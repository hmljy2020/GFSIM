#include <cstdlib>
#include <map>
#include <sstream>

#include "MeshBench.h"
#include "MeshNode.h"

namespace {
using namespace mesh_example;

struct Fixture {
    explicit Fixture(bool reverse = false, std::size_t workers = 0) : bench(std::make_shared<MeshBench>(&sim, reverse))
    {
        sim.AddModule(bench);
        sim.SetDeadlockThreshold(32);
        if (workers) {
            sim.SetWorkerCount(workers);
            sim.SetParallelChildren(bench.get());
        }
        sim.Build();
        for (auto* obj : bench->Objects()) {
            if (auto* queue = dynamic_cast<gfsim::SimQueue<Packet>*>(obj)) queues.emplace(obj->Name(), queue);
        }
    }
    gfsim::SimSystem sim;
    std::shared_ptr<MeshBench> bench;
    std::map<std::string, gfsim::SimQueue<Packet>*> queues;
};

void CheckTopology(Fixture& f)
{
    GFSIM_ASSERT(f.bench->Objects().size() == 254 && f.queues.size() == 214);
    GFSIM_ASSERT(f.bench->Submodules().size() == 40);
    for (int a = 0; a < kNodes; ++a) {
        const auto& child = f.bench->Submodules().at(a);
        GFSIM_ASSERT(dynamic_cast<MeshNode*>(child.get()) != nullptr);
        GFSIM_ASSERT(child->Name() == "node_" + std::to_string(a));
        GFSIM_ASSERT(child->Parent() == f.bench.get() && child->GetSim() == &f.sim);
        GFSIM_ASSERT(f.queues.count("inject_" + std::to_string(a)) == 1);
        GFSIM_ASSERT(f.queues.count("deliver_" + std::to_string(a)) == 1);
        for (int b = 0; b < kNodes; ++b) {
            const bool adjacent = std::abs(a % 8 - b % 8) + std::abs(a / 8 - b / 8) == 1;
            const auto name = "link_" + std::to_string(a) + "_to_" + std::to_string(b);
            GFSIM_ASSERT((f.queues.count(name) == 1) == adjacent) << name;
        }
    }
    for (const auto& entry : f.queues) {
        GFSIM_ASSERT(entry.second->MaxSize() == 2) << entry.first;
    }
}

void CheckComplete(const Fixture& f)
{
    GFSIM_ASSERT(f.sim.NeedTerminate() && f.sim.TerminationReason() == "mesh complete");
    GFSIM_ASSERT(f.bench->StimuliSent() == 1560 && f.bench->OutputsChecked() == 1560);
    GFSIM_ASSERT(f.bench->TotalHops() == 6760);
    GFSIM_ASSERT(f.sim.Cycles() == 132);
    for (const auto& entry : f.queues) {
        GFSIM_ASSERT(entry.second->WRBothEmpty()) << entry.first;
    }
    std::uint64_t latency = 0;
    for (const auto& t : f.bench->Timings()) {
        GFSIM_ASSERT(t.injected && t.delivered > t.injected);
        latency += t.delivered - t.injected;
    }
    GFSIM_ASSERT(latency == 19246) << "total packet latency in cycles";
}

void TestAllPairs(const char* dotPath)
{
    Fixture f;
    CheckTopology(f);
    f.sim.SaveDot(dotPath);
    f.sim.Run(10000);
    CheckComplete(f);
    const auto expected = f.bench->Timings();
    const auto cycles = f.sim.Cycles();
    GFSIM_ASSERT(expected[0].injected == 1 && expected[0].delivered == 4);
    std::ostringstream report;
    f.sim.GetReporter().SetOutput(report);
    f.sim.ReportStat();
    GFSIM_ASSERT(report.str().find("Delivered inter-node hops") != std::string::npos);
    GFSIM_ASSERT(report.str().find("Mean latency (cycles/delivered packet)") != std::string::npos);

    f.sim.Reset();
    GFSIM_ASSERT(!f.sim.NeedTerminate() && f.bench->TotalHops() == 0);
    for (const auto& t : f.bench->Timings()) GFSIM_ASSERT(!t.injected && !t.delivered);
    Fixture reversed(true);
    for (Fixture* run : {&f, &reversed}) {
        run->sim.Run(10000);
        CheckComplete(*run);
        GFSIM_ASSERT(run->sim.Cycles() == cycles);
        for (int id = 0; id < kPackets; ++id) {
            const auto& t = run->bench->Timings()[id];
            GFSIM_ASSERT(t.injected == expected[id].injected && t.delivered == expected[id].delivered)
                << "packet " << id;
        }
    }
}

void TestArbitrationAndBackpressure(std::size_t workers)
{
    Fixture f(true, workers); // Downstream node 10 runs before producer 9.
    std::ostringstream log, trace;
    f.sim.GetLogger().SetOutput(log);
    f.sim.GetLogger().SetLevel(gfsim::LogLevel::Debug);
    for (int i = 0; i < kNodes; ++i) f.queues.at("inject_" + std::to_string(i))->setStall();

    // Two independent input streams request node 9's east port for destination 19.
    auto& west = *f.queues.at("link_8_to_9");
    auto& north = *f.queues.at("link_1_to_9");
    auto& east = *f.queues.at("link_9_to_10");
    const auto firstWest = std::make_shared<Packet>(Packet{0, 8, 19, 1});
    const auto nextWest = std::make_shared<Packet>(Packet{1, 8, 19, 1});
    const auto firstNorth = std::make_shared<Packet>(Packet{2, 1, 19, 1});
    west.Write(firstWest);
    west.Write(nextWest);
    north.Write(firstNorth);
    west.Work();
    north.Work();
    east.setStall();
    f.sim.EnableTrace(trace);
    f.sim.Step();
    GFSIM_ASSERT(east.Empty() && east.SizeW() == 1 && west.Front() == nextWest);
    GFSIM_ASSERT(north.Front() == firstNorth && firstWest->hops == 2);
    f.sim.Step();
    GFSIM_ASSERT(east.Full() && north.Empty() && west.Front() == nextWest);
    GFSIM_ASSERT(firstNorth->hops == 2);
    const auto staged = east.Snapshot();
    GFSIM_ASSERT(staged.staged.at(0) == "id=0 src=8 dst=19 hops=2");
    GFSIM_ASSERT(staged.staged.at(1) == "id=2 src=1 dst=19 hops=2");
    east.unsetStall();
    f.sim.Step();
    GFSIM_ASSERT(east.Front() == firstWest && east.Snapshot().visible == staged.staged);
    f.sim.Step(); // Node 10 takes a packet, but node 9 saw a full link at cycle start.
    GFSIM_ASSERT(east.Front() == firstNorth && west.Front() == nextWest);
    GFSIM_ASSERT(nextWest->id == 1 && nextWest->source == 8 && nextWest->destination == 19 &&
                 nextWest->hops == 1);
    GFSIM_ASSERT(f.queues.at("link_10_to_11")->Front() == firstWest);
    f.sim.Step();
    GFSIM_ASSERT(west.Empty() && east.Front() == nextWest && nextWest->hops == 2);
    GFSIM_ASSERT(f.queues.at("link_11_to_19")->Front() == firstWest && firstWest->hops == 4);
    f.sim.DisableTrace();
    GFSIM_ASSERT(trace.str().find("\"name\":\"forward\"") != std::string::npos);
    GFSIM_ASSERT(log.str().find("[node_9.blocked][DEBUG] link_9_to_10") != std::string::npos);
}

void TestCapacityAtWork(std::size_t workers)
{
    Fixture f(true, workers);
    for (int i = 0; i < kNodes; ++i) f.queues.at("inject_" + std::to_string(i))->setStall();
    auto& input = *f.queues.at("link_8_to_9");
    auto& output = *f.queues.at("link_9_to_10");
    const auto packet = std::make_shared<Packet>(Packet{0, 8, 19, 1});
    input.Write(packet);
    input.Work();
    // Fill the output after Build/Reset, without a node Xfer capacity sample.
    output.Write(std::make_shared<Packet>(Packet{1, 8, 19, 2}));
    output.Write(std::make_shared<Packet>(Packet{2, 8, 19, 2}));
    output.setStall();
    f.sim.Step();
    GFSIM_ASSERT(input.Front() == packet && packet->hops == 1 && output.SizeW() == 2);
    // An idle queue reset makes space immediately for the next Work check.
    output.Reset();
    f.sim.Step();
    GFSIM_ASSERT(input.Empty() && output.Front() == packet && packet->hops == 2);
}

void TestWrongDelivery(std::size_t workers)
{
    Fixture f(false, workers);
    f.sim.Step();
    // Divert the first packet to node zero's local output.
    f.queues.at("inject_0")->Front()->destination = 0;
    f.sim.Step();
    try {
        f.sim.Step();
    } catch (const gfsim::AssertionError& error) {
        GFSIM_ASSERT(std::string(error.what()).find("delivered on port 0") != std::string::npos);
        GFSIM_ASSERT(f.bench->OutputsChecked() == 0 && !f.queues.at("deliver_0")->Empty());
        return;
    }
    GFSIM_ASSERT(false) << "expected the checker to reject the diverted packet";
}

void TestDeadlock(std::size_t workers)
{
    Fixture f(false, workers);
    f.sim.GetLogger().SetLevel(gfsim::LogLevel::Off);
    f.queues.at("deliver_0")->setStall();
    try {
        f.sim.Run(10000);
    } catch (const gfsim::DeadlockError&) {
        GFSIM_ASSERT(f.sim.IsDeadlocked() && f.sim.StalledCycles() == 32);
        GFSIM_ASSERT(!f.sim.NeedTerminate() && f.bench->OutputsChecked() < kPackets);
        return;
    }
    GFSIM_ASSERT(false) << "permanent delivery blockage should deadlock";
}

void TestParallelEquivalence()
{
    Fixture serial, one(false, 1), two(false, 2), four(false, 4);
    const std::vector<Fixture*> parallel{&one, &two, &four};
    for (int pass = 0; pass < 2; ++pass) {
        while (!serial.sim.NeedTerminate()) {
            serial.sim.Step();
            for (auto* f : parallel) {
                f->sim.Step();
                GFSIM_ASSERT(f->sim.NeedTerminate() == serial.sim.NeedTerminate());
                GFSIM_ASSERT(f->sim.Cycles() == serial.sim.Cycles());
                GFSIM_ASSERT(f->bench->TotalHops() == serial.bench->TotalHops());
                GFSIM_ASSERT(f->bench->StimuliSent() == serial.bench->StimuliSent());
                GFSIM_ASSERT(f->bench->OutputsChecked() == serial.bench->OutputsChecked());
                for (const auto& entry : serial.queues) {
                    const auto expected = entry.second->Snapshot();
                    const auto actual = f->queues.at(entry.first)->Snapshot();
                    GFSIM_ASSERT(actual.visible == expected.visible && actual.staged == expected.staged &&
                                 actual.delays == expected.delays && actual.capacity == expected.capacity &&
                                 actual.stalled == expected.stalled) << entry.first << " cycle " << serial.sim.Cycles();
                }
                for (int id = 0; id < kPackets; ++id) {
                    const auto& expected = serial.bench->Timings()[id];
                    const auto& actual = f->bench->Timings()[id];
                    GFSIM_ASSERT(actual.injected == expected.injected && actual.delivered == expected.delivered);
                }
            }
        }
        CheckComplete(serial);
        std::ostringstream expectedReport;
        serial.sim.GetReporter().SetOutput(expectedReport);
        serial.sim.ReportStat();
        for (auto* f : parallel) {
            CheckComplete(*f);
            std::ostringstream actualReport;
            f->sim.GetReporter().SetOutput(actualReport);
            f->sim.ReportStat();
            GFSIM_ASSERT(actualReport.str() == expectedReport.str());
            f->sim.Reset();
        }
        serial.sim.Reset();
    }
}

} // namespace

int main(int argc, char** argv)
{
    GFSIM_ASSERT(argc == 2) << "expected DOT output path";
    TestAllPairs(argv[1]);
    TestParallelEquivalence();
    for (std::size_t workers : {0u, 1u, 2u, 4u}) {
        TestArbitrationAndBackpressure(workers);
        TestCapacityAtWork(workers);
        TestWrongDelivery(workers);
        TestDeadlock(workers);
    }
}
