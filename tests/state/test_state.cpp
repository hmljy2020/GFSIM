#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <vector>
#include "SimAssert.h"
#include "StateOwner.h"

using namespace gfsim;
using namespace state_test;

static_assert(std::is_base_of<SimObj, SimState<int>>::value, "state derives from SimObj");
static_assert(!std::is_base_of<SimModule, SimState<int>>::value, "state is not a module");
static_assert(std::is_same<decltype(std::declval<const SimState<int>&>().Read()),
                           int>::value, "reads return values");

enum class Mode { Idle, Busy, Done };

// No default constructor: state must initialize every value from initialValue.
struct Value {
    explicit Value(int n) : number(n), tag(7) {}
    int number;
    int tag;
    bool operator==(const Value& other) const
    { return number == other.number && tag == other.tag; }
};

template <typename T>
void ValueSemantics(const T& initial, const T& first, const T& last)
{
    SimState<T> state("standalone_state", initial);
    GFSIM_ASSERT(state.Name() == "standalone_state" && state.Read() == initial);
    GFSIM_ASSERT(state.GetSim() == nullptr && state.Objects().empty());
    state.Build();
    state.Work();
    state.ReportStat();
    state.Xfer();
    GFSIM_ASSERT(state.Read() == initial);
    state.Write(first);
    state.Write(last);
    GFSIM_ASSERT(state.Read() == initial);
    state.Xfer();
    GFSIM_ASSERT(state.Read() == last);
    state.Xfer();
    GFSIM_ASSERT(state.Read() == last);
    state.Write(first);
    state.Reset();
    GFSIM_ASSERT(state.Read() == initial);
    state.Xfer();
    GFSIM_ASSERT(state.Read() == initial) << "Reset must discard pending writes";
    state.Write(first);
    state.Xfer();
    GFSIM_ASSERT(state.Read() == first);
    state.Reset();
    GFSIM_ASSERT(state.Read() == initial);
}

void FieldSemantics()
{
    bool rejected = false;
    try { SimState<int> invalid("", 0); }
    catch (const std::invalid_argument&) { rejected = true; }
    GFSIM_ASSERT(rejected) << "empty state name accepted";
    ValueSemantics<int>(-3, 4, 8);
    ValueSemantics<std::uint64_t>(0x1000, 0x1004, 0x1008);
    ValueSemantics<double>(1.25, 2.5, 3.75);
    ValueSemantics<bool>(true, true, false);
    ValueSemantics<Mode>(Mode::Idle, Mode::Busy, Mode::Done);
    ValueSemantics<Value>(Value(1), Value(2), Value(3));

    SimState<int> count("count", 5), untouched("untouched", 17);
    count.Build();
    untouched.Build();
    count.Write(count.Read() + 1);
    count.Write(count.Read() + 1);
    count.Xfer();
    untouched.Xfer();
    GFSIM_ASSERT(count.Read() == 6 && untouched.Read() == 17);

    Value initial(11);
    SimState<Value> aggregate("aggregate", initial);
    aggregate.Build();
    initial.number = 99;
    Value copy = aggregate.Read();
    copy.number = 22;
    copy.tag = 8;
    GFSIM_ASSERT(aggregate.Read() == Value(11)) << "Read must return a copy";
    aggregate.Write(copy);
    copy.number = 33;
    GFSIM_ASSERT(aggregate.Read() == Value(11));
    aggregate.Xfer();
    GFSIM_ASSERT(aggregate.Read().number == 22 && aggregate.Read().tag == 8);
    aggregate.Reset();
    GFSIM_ASSERT(aggregate.Read() == Value(11)) << "initial value is saved by value";
}

std::vector<std::uint64_t> Run(std::size_t workers, const std::string& dotPath)
{
    SimSystem sim;
    std::ostringstream log, report;
    sim.GetLogger().SetOutput(log);
    sim.GetLogger().SetLevel(LogLevel::Debug);
    sim.GetReporter().SetOutput(report);
    auto owner = std::make_shared<StateOwner>(&sim);
    sim.AddModule(owner);
    if (workers != 0) {
        sim.SetWorkerCount(workers);
        sim.SetParallelChildren(owner.get());
    }
    sim.SetDeadlockThreshold(2);
    sim.Build();
    GFSIM_ASSERT(owner->Objects().size() == 2 && owner->Submodules().size() == 2);
    for (std::size_t i = 0; i < 2; ++i) {
        GFSIM_ASSERT(owner->Objects()[i] == &owner->Access(i));
        GFSIM_ASSERT(owner->Access(i).Objects().empty());
        GFSIM_ASSERT(owner->Access(i).Parent() == owner.get());
    }
    std::ostringstream dot;
    sim.WriteDot(dot);
    for (const std::string name : {"shared_pc", "shared_count", "auxiliary_count",
                                   "bank_pc_0", "bank_pc_1", "bank_count_0", "bank_count_1"})
        GFSIM_ASSERT(dot.str().find(name) == std::string::npos) << name << " leaked into DOT";
    for (const std::string name : {"state_owner", "pc_writer", "count_writer"})
        GFSIM_ASSERT(dot.str().find(name) != std::string::npos) << name << " missing from DOT";
    if (!dotPath.empty()) sim.SaveDot(dotPath);

    std::vector<std::uint64_t> values;
    for (unsigned pass = 0; pass < 2; ++pass) {
        GFSIM_ASSERT(owner->Pc().Read() == 0 && owner->Count().Read() == 0);
        GFSIM_ASSERT(owner->Auxiliary().Read() == 0);
        for (std::size_t i = 0; i < 2; ++i) {
            GFSIM_ASSERT(owner->BankPc(i).Read() == 0 && owner->BankCount(i).Read() == 0);
            GFSIM_ASSERT(owner->Access(i).Observations().empty());
        }
        std::uint64_t pc = 0, count = 0;
        for (std::uint64_t cycle = 1; cycle <= 32; ++cycle) {
            sim.Step();
            for (std::size_t i = 0; i < 2; ++i) {
                const auto& access = owner->Access(i);
                GFSIM_ASSERT(access.Observations().size() == cycle);
                const auto& observed = access.Observations().back();
                GFSIM_ASSERT(observed.pc == pc && observed.count == count)
                    << "both workers must see the same old values";
                GFSIM_ASSERT(access.WorkThread() == access.XferThread());
                GFSIM_ASSERT((access.WorkThread() == std::this_thread::get_id()) == (workers == 0));
                GFSIM_ASSERT(owner->BankPc(i).Read() == cycle * (i + 1));
                GFSIM_ASSERT(owner->BankCount(i).Read() == (cycle / 2) * (i + 3));
            }
            GFSIM_ASSERT((owner->Access(0).WorkThread() == owner->Access(1).WorkThread()) ==
                         (workers < 2));
            const std::uint64_t priorPc = pc;
            pc += count + 1;
            count += priorPc + 2;
            GFSIM_ASSERT(owner->Pc().Read() == pc && owner->Count().Read() == count);
            GFSIM_ASSERT(owner->Auxiliary().Read() == cycle);
            GFSIM_ASSERT(sim.Cycles() == cycle && sim.StalledCycles() == 0);
            GFSIM_ASSERT(sim.NeedTerminate() == (cycle == 32));
            values.push_back(pc);
            values.push_back(count);
            values.push_back(owner->BankPc(0).Read());
            values.push_back(owner->BankCount(1).Read());
        }
        GFSIM_ASSERT(sim.TerminationReason() == "state sample complete");
        sim.ReportStat();
        GFSIM_ASSERT(report.str().find("Owner work cycles (cycles since reset)") != std::string::npos);
        GFSIM_ASSERT(report.str().find("pc_writer current snapshots") != std::string::npos);
        GFSIM_ASSERT(report.str().find("count_writer current snapshots") != std::string::npos);
        GFSIM_ASSERT(log.str().find("[C:32][state_owner.work][DEBUG] work_cycles=32\n") != std::string::npos);
        GFSIM_ASSERT(log.str().find("[pc_writer.work][DEBUG] staged field update") != std::string::npos);
        GFSIM_ASSERT(log.str().find("[count_writer.work][DEBUG] staged field update") != std::string::npos);
        sim.Reset();
        GFSIM_ASSERT(!sim.NeedTerminate() && sim.Cycles() == 0);
    }
    return values;
}

void NoProgress()
{
    for (std::size_t workers : {0u, 2u}) {
        SimSystem sim;
        auto owner = std::make_shared<StateOwner>(&sim, 0, false);
        sim.AddModule(owner);
        if (workers != 0) {
            sim.SetWorkerCount(workers);
            sim.SetParallelChildren(owner.get());
        }
        sim.SetDeadlockThreshold(2);
        sim.Build();
        bool deadlocked = false;
        try { sim.Run(3); }
        catch (const DeadlockError&) { deadlocked = true; }
        GFSIM_ASSERT(deadlocked && sim.IsDeadlocked() && sim.Cycles() == 2);
        GFSIM_ASSERT(owner->Pc().Read() == 0 && owner->Count().Read() == 0);
        GFSIM_ASSERT(owner->Auxiliary().Read() == 2) << "bookkeeping is not useful progress";
        sim.Reset();
        GFSIM_ASSERT(!sim.IsDeadlocked() && sim.StalledCycles() == 0);
    }
}

int main(int argc, char** argv)
{
    GFSIM_ASSERT(argc == 2) << "expected output DOT path";
    FieldSemantics();
    const auto serial = Run(0, argv[1]);
    GFSIM_ASSERT(Run(1, "") == serial);
    GFSIM_ASSERT(Run(2, "") == serial);
    NoProgress();
}
