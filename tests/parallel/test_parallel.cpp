#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include "ProbeModule.h"
#include "RingStage.h"
#include "SimExecutor.h"

namespace {
using namespace gfsim;
using namespace parallel_test;

template<class Error, class F> void Expect(F run)
{
    bool caught = false;
    try { run(); } catch (const Error&) { caught = true; }
    GFSIM_ASSERT(caught) << "expected exception";
}

std::shared_ptr<ProbeModule> Root(SimSystem& sim, std::size_t count)
{
    std::vector<std::shared_ptr<SimModule>> children;
    for (std::size_t i = 0; i < count; ++i)
        children.push_back(std::make_shared<ProbeModule>("child" + std::to_string(i)));
    auto root = std::make_shared<ProbeModule>("root", &sim, children);
    sim.AddModule(root);
    return root;
}

// A bounded rendezvous proves overlap, and fails rather than hanging if serialized.
class Gate {
public:
    void Meet()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        const unsigned generation = generation_;
        if (++arrived_ == 2) {
            arrived_ = 0;
            ++generation_;
            cv_.notify_all();
        } else {
            GFSIM_ASSERT(cv_.wait_for(lock, std::chrono::seconds(5), [&] {
                return generation_ != generation;
            })) << "two workers did not overlap";
        }
    }
private:
    std::mutex mutex_;
    std::condition_variable cv_;
    unsigned arrived_ = 0;
    unsigned generation_ = 0;
};

// Queue instrumentation is read by the controller only after a phase barrier.
class ObservedQueue final : public SimQueue<RingPacket> {
public:
    explicit ObservedQueue(std::string name) : SimQueue<RingPacket>(std::move(name)) {}
    void Work() override {
        thread_ = std::this_thread::get_id();
        if (hook_) hook_();
        SimQueue<RingPacket>::Work();
        ++advances_;
    }
    void Reset() override {
        SimQueue<RingPacket>::Reset();
        advances_ = 0;
        thread_ = std::thread::id();
    }
    void SetHook(std::function<void()> hook) { hook_ = std::move(hook); }
    std::thread::id Thread() const { return thread_; }
    unsigned Advances() const { return advances_; }
private:
    std::function<void()> hook_;
    std::thread::id thread_;
    unsigned advances_ = 0;
};

void Configuration()
{
    SimSystem empty;
    Expect<std::logic_error>([&] { empty.Build(); });
    Expect<std::logic_error>([&] { empty.SetWorkerCount(2); });
    SimSystem invalid;
    Expect<std::invalid_argument>([&] { invalid.SetWorkerCount(0); });
    Expect<std::invalid_argument>([&] { invalid.SetParallelChildren(nullptr); });
    auto r = Root(invalid, 0);
    invalid.SetWorkerCount(2);
    Expect<std::logic_error>([&] { invalid.Build(); });
    Expect<std::logic_error>([&] { invalid.SetParallelChildren(r.get()); });
    Expect<std::logic_error>([&] { invalid.SetWorkerCount(1); });
    Expect<std::logic_error>([&] { invalid.Step(); });
    for (int mode = 0; mode < 3; ++mode) {
        SimSystem sim, other;
        auto root = Root(sim, 0);
        auto outsider = std::make_shared<ProbeModule>("outsider", &sim);
        auto missingContext = std::make_shared<ProbeModule>("missing");
        auto wrongContext = std::make_shared<ProbeModule>("wrong", &other);
        auto split = mode == 0 ? outsider : mode == 1 ? missingContext : wrongContext;
        if (mode != 0) sim.AddModule(split);
        sim.SetParallelChildren(split.get());
        Expect<std::logic_error>([&] { sim.Build(); });
    }
    for (std::size_t count : {0u, 1u, 5u}) {
        for (std::size_t workers : {1u, 2u, 4u, 8u}) {
            SimSystem sim;
            auto root = Root(sim, count);
            sim.SetWorkerCount(workers);
            sim.SetParallelChildren(root.get());
            sim.Build();
            Expect<std::logic_error>([&] { sim.SetWorkerCount(workers); });
            Expect<std::logic_error>([&] { sim.SetParallelChildren(root.get()); });
            std::vector<std::thread::id> owners(count);
            for (std::size_t i = 0; i < count; ++i) {
                auto child = std::static_pointer_cast<ProbeModule>(root->Submodules()[i]);
                child->SetHook([&, i](ProbeModule&, bool, bool) {
                    const auto here = std::this_thread::get_id();
                    if (owners[i] == std::thread::id()) owners[i] = here;
                    GFSIM_ASSERT(owners[i] == here);
                });
            }
            for (int pass = 0; pass < 2; ++pass) {
                for (int cycle = 0; cycle < 3; ++cycle) sim.Step();
                GFSIM_ASSERT(root->Works() == 3 && root->Xfers() == 3);
                for (std::size_t i = 0; i < count; ++i) {
                    auto child = std::static_pointer_cast<ProbeModule>(root->Submodules()[i]);
                    GFSIM_ASSERT(child->Works() == 3 && child->Xfers() == 3);
                    GFSIM_ASSERT(owners[i] != std::this_thread::get_id());
                    for (std::size_t j = 0; j < count; ++j) {
                        GFSIM_ASSERT((owners[i] == owners[j]) ==
                                     (i % std::min(count, workers) == j % std::min(count, workers)));
                    }
                }
                sim.Reset(); // Worker identity must survive Reset.
            }
        }
    }
}

void Phases(const std::string& tracePath)
{
    SimSystem sim;
    auto aLeaf = std::make_shared<ProbeModule>("a_leaf");
    auto bLeaf = std::make_shared<ProbeModule>("b_leaf");
    auto a = std::make_shared<ProbeModule>("a", nullptr, std::vector<std::shared_ptr<SimModule>>{aLeaf});
    auto b = std::make_shared<ProbeModule>("b", nullptr, std::vector<std::shared_ptr<SimModule>>{bLeaf});
    auto split = std::make_shared<ProbeModule>("split", nullptr, std::vector<std::shared_ptr<SimModule>>{a, b});
    auto root = std::make_shared<ProbeModule>("root", &sim, std::vector<std::shared_ptr<SimModule>>{split});
    auto last = std::make_shared<ProbeModule>("last", &sim);
    sim.AddModule(root);
    sim.AddModule(last);
    sim.SetWorkerCount(2);
    sim.SetParallelChildren(split.get()); // Nested split, known before Build.
    sim.SetDeadlockThreshold(1);
    sim.Build();
    Gate gate;
    std::mutex mutex;
    struct Event { std::string name; bool work; bool after; std::uint64_t cycle; std::thread::id thread; };
    std::vector<Event> events;
    const std::vector<std::shared_ptr<ProbeModule>> nodes{root, split, a, aLeaf, b, bLeaf, last};
    for (const auto& node : nodes) {
        node->SetHook([&](ProbeModule& module, bool work, bool after) {
            if (!after && (module.Name() == "a" || module.Name() == "b")) gate.Meet();
            if (work && !after) sim.MarkProgress(); // Completed probe operation.
            std::lock_guard<std::mutex> lock(mutex);
            events.push_back(Event{module.Name(), work, after, sim.Cycles(), std::this_thread::get_id()});
        });
    }
    std::ostringstream log, report;
    std::ofstream trace(tracePath.c_str());
    sim.GetLogger().SetOutput(log);
    sim.GetLogger().SetLevel(LogLevel::Debug);
    sim.EnableTrace(trace);
    for (int i = 0; i < 3; ++i) sim.Step();
    sim.DisableTrace();
    sim.GetReporter().SetOutput(report);
    sim.ReportStat();
    GFSIM_ASSERT(report.str().find("a_leaf Work calls (calls since reset)") != std::string::npos);
    GFSIM_ASSERT(sim.StalledCycles() == 0);
    GFSIM_ASSERT(events.size() == nodes.size() * 4 * 3);
    for (std::uint64_t cycle = 1; cycle <= 3; ++cycle) {
        auto position = [&](const std::string& name, bool work, bool after) {
            for (std::size_t i = 0; i < events.size(); ++i) {
                const auto& e = events[i];
                if (e.name == name && e.work == work && e.after == after && e.cycle == cycle) return i;
            }
            throw std::runtime_error("missing phase event");
        };
        for (bool work : {true, false}) {
            GFSIM_ASSERT(position("root", work, false) < position("split", work, false));
            for (const std::string name : {"a", "b"}) {
                GFSIM_ASSERT(position("split", work, false) < position(name, work, false));
                GFSIM_ASSERT(position(name, work, false) < position(name + "_leaf", work, false));
                GFSIM_ASSERT(position(name + "_leaf", work, true) < position(name, work, true));
                GFSIM_ASSERT(position(name, work, true) < position("split", work, true));
                GFSIM_ASSERT(events[position(name, work, false)].thread ==
                             events[position(name + "_leaf", work, false)].thread);
            }
            GFSIM_ASSERT(position("split", work, true) < position("root", work, true));
            GFSIM_ASSERT(position("root", work, true) < position("last", work, false));
        }
        GFSIM_ASSERT(position("last", true, true) < position("root", false, false));
        if (cycle < 3) {
            const auto end = position("last", false, true);
            GFSIM_ASSERT(events[end + 1].cycle == cycle + 1 && events[end + 1].work);
        }
        for (const auto& node : nodes) {
            const std::string line = "[C:" + std::to_string(cycle) + "][" + node->Name() +
                ".work][DEBUG] operations=" + std::to_string(cycle) + "\n";
            GFSIM_ASSERT(log.str().find(line) != std::string::npos) << line;
        }
    }
    const std::string logged = log.str();
    GFSIM_ASSERT(std::count(logged.begin(), logged.end(), '\n') == 21);
}

void Termination()
{
    for (std::size_t workers : {1u, 2u, 4u}) {
        for (int scenario = 0; scenario < 6; ++scenario) {
            SimSystem sim;
            auto root = Root(sim, 4);
            sim.SetWorkerCount(workers);
            sim.SetParallelChildren(root.get());
            sim.Build();
            for (std::size_t i = 0; i < 4; ++i) {
                std::static_pointer_cast<ProbeModule>(root->Submodules()[i])->SetHook(
                    [&, i](ProbeModule&, bool work, bool after) {
                        if (after) return;
                        if (scenario >= 4 && work) return;
                        if (scenario == 1 && i == 0 && work) return;
                        sim.RequestTerminate(std::string(work ? "work" : "xfer") + std::to_string(i));
                        sim.RequestTerminate("later request");
                        GFSIM_ASSERT(!sim.NeedTerminate() && sim.TerminationReason().empty());
                    });
            }
            root->SetHook([&](ProbeModule&, bool work, bool after) {
                if (!after && ((scenario == 2 && work) ||
                               ((scenario == 3 || scenario == 5) && !work))) {
                    sim.RequestTerminate("main");
                    GFSIM_ASSERT(!sim.NeedTerminate());
                }
            });
            for (int repeat = 0; repeat < 4; ++repeat) {
                sim.Step();
                GFSIM_ASSERT(sim.NeedTerminate());
                const std::string expected = scenario == 1 ? "work1" :
                    (scenario == 2 || scenario == 5) ? "main" : scenario == 4 ? "xfer0" : "work0";
                GFSIM_ASSERT(sim.TerminationReason() == expected) << sim.TerminationReason();
                for (const auto& child : root->Submodules())
                    GFSIM_ASSERT(std::static_pointer_cast<ProbeModule>(child)->Xfers() == 1);
                Expect<std::logic_error>([&] { sim.Step(); });
                sim.Reset();
            }
            sim.RequestTerminate("outside step");
            GFSIM_ASSERT(sim.NeedTerminate() && sim.TerminationReason() == "outside step");
        }
    }
    SimSystem serial;
    auto root = Root(serial, 0);
    root->SetHook([&](ProbeModule&, bool work, bool after) {
        if (work && !after) {
            serial.RequestTerminate("immediate");
            GFSIM_ASSERT(serial.NeedTerminate());
            serial.RequestTerminate("last serial request");
        }
    });
    serial.Build();
    serial.Step();
    GFSIM_ASSERT(serial.TerminationReason() == "last serial request");
}

void Failures()
{
    for (bool workFailure : {true, false}) {
        SimSystem sim;
        auto root = Root(sim, 4);
        sim.SetParallelChildren(root.get());
        sim.SetWorkerCount(2);
        sim.Build();
        Gate gate;
        bool fail = true;
        std::atomic<bool> otherFinished{false};
        std::static_pointer_cast<ProbeModule>(root->Submodules()[0])->SetHook(
            [&](ProbeModule&, bool work, bool after) {
                if (fail && work == workFailure && !after) {
                    gate.Meet();
                    sim.RequestTerminate("discard on failure");
                    if (work) throw std::runtime_error("work failure");
                    GFSIM_ASSERT(false) << "xfer failure";
                }
            });
        std::static_pointer_cast<ProbeModule>(root->Submodules()[1])->SetHook(
            [&](ProbeModule&, bool work, bool after) {
                if (fail && work == workFailure) {
                    if (!after) gate.Meet();
                    else otherFinished.store(true);
                }
            });
        if (workFailure) Expect<std::runtime_error>([&] { sim.Step(); });
        else Expect<AssertionError>([&] { sim.Step(); });
        GFSIM_ASSERT(otherFinished.load() && !sim.NeedTerminate() && sim.TerminationReason().empty());
        auto skipped = std::static_pointer_cast<ProbeModule>(root->Submodules()[2]);
        GFSIM_ASSERT(workFailure ? skipped->Works() == 0 : skipped->Xfers() == 0);
        if (workFailure) for (const auto& child : root->Submodules())
            GFSIM_ASSERT(std::static_pointer_cast<ProbeModule>(child)->Xfers() == 0);
        Expect<std::logic_error>([&] { sim.Step(); });
        root->SetResetHook([] { throw std::runtime_error("reset failure"); });
        Expect<std::runtime_error>([&] { sim.Reset(); });
        Expect<std::logic_error>([&] { sim.Step(); });
        root->SetResetHook({});
        fail = false;
        sim.Reset();
        sim.Step();
        GFSIM_ASSERT(!sim.NeedTerminate() && skipped->Works() == 1 && skipped->Xfers() == 1);
        root->SetHook([](ProbeModule&, bool work, bool after) {
            if (work && after) throw std::runtime_error("main failure");
        });
        Expect<std::runtime_error>([&] { sim.Step(); });
        Expect<std::logic_error>([&] { sim.Step(); });
        root->SetHook({});
        sim.Reset();
        sim.Step();
    }
    // Deterministically fail the second thread launch; the first must be joined.
    SimSystem sim;
    auto root = Root(sim, 2);
    sim.Build();
    std::atomic<int> started{0}, exited{0};
    int launches = 0;
    Expect<std::runtime_error>([&] {
        SimExecutor executor(&sim, SimExecutionPlan(root.get(), 2),
            [&](std::function<void()> run) -> std::thread {
                if (++launches == 2) throw std::runtime_error("injected launch failure");
                return std::thread([&, run] { ++started; run(); ++exited; });
            });
    });
    GFSIM_ASSERT(started == 1 && exited == 1);
    {
        SimExecutor executor(&sim, SimExecutionPlan(root.get(), 2),
            [&](std::function<void()> run) -> std::thread {
                return std::thread([&, run] { ++started; run(); ++exited; });
            });
        executor.Execute(SimPhase::Work);
        executor.Execute(SimPhase::Xfer);
    }
    GFSIM_ASSERT(started == 3 && exited == 3);
}

void Queues(const std::string& tracePath, const std::string& dotPath)
{
    for (std::size_t workers : {1u, 2u, 4u}) {
        SimSystem sim;
        auto q0 = std::make_shared<SimQueue<RingPacket>>("to_b");
        auto q1 = std::make_shared<SimQueue<RingPacket>>("to_a");
        auto a = std::make_shared<RingStage>("a", q1.get(), q0);
        auto b = std::make_shared<RingStage>("b", q0.get(), q1);
        auto root = std::make_shared<ProbeModule>("ring", &sim, std::vector<std::shared_ptr<SimModule>>{a, b});
        sim.AddModule(root);
        sim.SetParallelChildren(root.get());
        sim.SetWorkerCount(workers);
        sim.SetDeadlockThreshold(8); // Two-cycle queues, maximum silent gap is one cycle.
        sim.Build();
        sim.SaveDot(dotPath);
        for (int pass = 0; pass < 2; ++pass) {
            auto packet = std::make_shared<RingPacket>(RingPacket{7, 0});
            q1->Write(packet);
            q1->Work(); q1->Work();
            q0->Write(std::make_shared<RingPacket>(RingPacket{99, 0}));
            q0->Work(); q0->Work();
            std::ostringstream log, report;
            std::ofstream trace(tracePath.c_str());
            sim.GetLogger().SetOutput(log);
            sim.GetLogger().SetLevel(LogLevel::Debug);
            sim.EnableTrace(trace);
            sim.Step(); // Consumer pops q0; its producer must still see it as full.
            GFSIM_ASSERT(q1->Front() == packet && packet->id == 7 && packet->hops == 0);
            GFSIM_ASSERT(q0->WRBothEmpty());
            sim.Step();
            GFSIM_ASSERT(q1->Empty() && packet->hops == 1);
            GFSIM_ASSERT(q0->Snapshot().staged == std::vector<std::string>{"7:1"});
            GFSIM_ASSERT(q0->Snapshot().delays == std::vector<std::uint32_t>{1});
            sim.Step();
            GFSIM_ASSERT(q0->Front() == packet && q0->Snapshot().delays.empty());
            sim.Run(20);
            GFSIM_ASSERT(sim.Cycles() == 10 && sim.TerminationReason() == "ring complete");
            GFSIM_ASSERT(q0->WRBothEmpty() && q1->WRBothEmpty() && packet->hops == 4);
            sim.DisableTrace();
            GFSIM_ASSERT(log.str().find("[C:1][a.blocked][DEBUG] output_full\n") != std::string::npos);
            sim.GetReporter().SetOutput(report);
            sim.ReportStat();
            GFSIM_ASSERT(report.str().find("forwarded packets (since reset)") != std::string::npos);
            sim.Reset();
        }
        sim.GetLogger().SetLevel(LogLevel::Off);
        Expect<DeadlockError>([&] { sim.Run(10); });
        GFSIM_ASSERT(sim.StalledCycles() == 8 && !sim.NeedTerminate());
        sim.Reset();
        GFSIM_ASSERT(!sim.IsDeadlocked());
    }
}
void DelegatedQueues(const std::string& dotPath)
{
    using Modules = std::vector<std::shared_ptr<SimModule>>;
    using Queues = std::vector<std::shared_ptr<SimQueue<RingPacket>>>;
    for (std::size_t workers : {1u, 2u, 4u}) {
        // 0: parent ownership; 1: external root, main-thread producer;
        // 2: physical owners and producers belong to different worker subtrees.
        for (int placement = 0; placement < 3; ++placement) {
            SimSystem sim;
            auto q0 = std::make_shared<ObservedQueue>("to_b");
            auto q1 = std::make_shared<ObservedQueue>("to_a");
            std::shared_ptr<ObservedQueue> q2;
            auto a = std::make_shared<RingStage>("a", q1.get(), q0, false);
            auto b = std::make_shared<RingStage>("b", q0.get(), q1, false);
            std::shared_ptr<ProbeModule> root;
            if (placement == 0) {
                root = std::make_shared<ProbeModule>("root", &sim, Modules{a, b}, Queues{q0, q1});
            } else if (placement == 1) {
                q2 = std::make_shared<ObservedQueue>("main_loop");
                auto nested = std::make_shared<ProbeModule>("nested", nullptr, Modules{a});
                root = std::make_shared<ProbeModule>("root", &sim, Modules{nested});
                auto mainStage = std::make_shared<RingStage>("main_stage", q2.get(), q2, false);
                auto outside = std::make_shared<ProbeModule>("outside", &sim, Modules{b, mainStage}, Queues{q0, q1, q2});
                // The queue owner runs before the split in both phases.
                sim.AddModule(outside);
            } else {
                auto branchA = std::make_shared<ProbeModule>("branch_a", nullptr, Modules{a}, Queues{q1});
                auto branchB = std::make_shared<ProbeModule>("branch_b", nullptr, Modules{b}, Queues{q0});
                root = std::make_shared<ProbeModule>("root", &sim, Modules{branchA, branchB});
            }
            sim.AddModule(root);
            sim.SetParallelChildren(root.get());
            sim.SetWorkerCount(workers);
            sim.SetDeadlockThreshold(8);
            sim.Build();
            sim.SaveDot(dotPath + ".delegated" + std::to_string(placement) + ".dot");
            Gate gate;
            for (int pass = 0; pass < 2; ++pass) {
                // Seed while idle, temporarily disabling the parallel rendezvous.
                q0->SetHook({}); q1->SetHook({});
                auto packet = std::make_shared<RingPacket>(RingPacket{7, 0});
                q1->Write(packet); q1->Work(); q1->Work();
                q0->Write(std::make_shared<RingPacket>(RingPacket{99, 0}));
                q0->Work(); q0->Work();
                if (workers > 1 && placement != 1) {
                    q0->SetHook([&] { gate.Meet(); });
                    q1->SetHook([&] { gate.Meet(); });
                }
                sim.Step();
                // The main-thread consumer in placement 1 has already popped
                // q0 before a.Work(): same-cycle admission must still block.
                GFSIM_ASSERT(q1->Front() == packet && packet->hops == 0);
                GFSIM_ASSERT(q0->Thread() == a->WorkThread());
                GFSIM_ASSERT(q1->Thread() == (placement == 1 ? a->WorkThread() : b->WorkThread()));
                GFSIM_ASSERT(q0->Thread() != std::this_thread::get_id());
                if (placement == 1) GFSIM_ASSERT(q2->Thread() == std::this_thread::get_id());
                sim.Step();
                GFSIM_ASSERT(q0->Snapshot().delays == std::vector<std::uint32_t>{1});
                while (!sim.NeedTerminate()) sim.Step();
                GFSIM_ASSERT(sim.Cycles() == 10 && packet->hops == 4);
                GFSIM_ASSERT(q0->Advances() == sim.Cycles() + 2);
                GFSIM_ASSERT(q1->Advances() == sim.Cycles() + 2);
                if (placement == 1) GFSIM_ASSERT(q2->Advances() == sim.Cycles());
                sim.Reset();
                GFSIM_ASSERT(q0->Advances() == 0 && q1->Advances() == 0);
            }
            if (workers == 2 && placement == 0) {
                q0->SetHook([&] {
                    gate.Meet();
                    sim.RequestTerminate("discard queue failure");
                    throw std::runtime_error("queue advance failed");
                });
                q1->SetHook([&] { gate.Meet(); });
                Expect<std::runtime_error>([&] { sim.Step(); });
                GFSIM_ASSERT(q1->Advances() == 1 && !sim.NeedTerminate());
                Expect<std::logic_error>([&] { sim.Step(); });
                q0->SetHook({}); q1->SetHook({});
                sim.Reset();
                sim.Step();
                GFSIM_ASSERT(q0->Advances() == 1 && q1->Advances() == 1);
            }
        }
    }
}

} // namespace
int main(int argc, char** argv)
{
    GFSIM_ASSERT(argc == 4);
    Configuration();
    Phases(argv[1]);
    Termination();
    Failures();
    Queues(argv[2], argv[3]);
    DelegatedQueues(argv[3]);
}
