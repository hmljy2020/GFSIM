#include <algorithm>
#include <sstream>
#include <vector>
#include "MethodBench.h"
#include "SimExecutor.h"

using namespace gfsim;
using namespace method_example;

std::vector<int> Run(std::size_t workers, const std::string& dotPath)
{
    std::ostringstream log, report;
    SimSystem sim;
    sim.GetLogger().SetOutput(log);
    sim.GetLogger().SetLevel(LogLevel::Debug);
    sim.GetReporter().SetOutput(report);
    auto bench = std::make_shared<MethodBench>(&sim);
    sim.AddModule(bench);
    if (workers != 0) {
        sim.SetWorkerCount(workers);
        sim.SetParallelChildren(bench.get());
    }
    sim.SetDeadlockThreshold(2);
    sim.Build();
    GFSIM_ASSERT(bench->Objects().size() == 3 && bench->Submodules().size() == 3);
    SimExecutionPlan plan(bench.get(), workers == 0 ? 1 : workers);
    unsigned tasks = 0;
    for (const auto& worker : plan.workers) tasks += worker.size();
    GFSIM_ASSERT(tasks == 3) << "only the three children are tasks";
    for (const auto& child : bench->Submodules()) {
        GFSIM_ASSERT(child->Parent() == bench.get() && child->GetSim() == &sim);
        GFSIM_ASSERT(child->Objects().empty() && child->Submodules().empty());
    }
    if (!dotPath.empty()) sim.SaveDot(dotPath);
    std::vector<int> values;
    for (unsigned pass = 0; pass < 2; ++pass) {
        GFSIM_ASSERT(bench->Target().Read(0) == 10 && bench->Target().Read(1) == 20);
        GFSIM_ASSERT(bench->Target().WorkCycles() == 0);
        GFSIM_ASSERT(bench->OutputsChecked() == 0 && bench->StimuliSent() == 0);
        for (unsigned field = 0; field < 2; ++field) {
            GFSIM_ASSERT(bench->Caller(field).Observations().empty());
            GFSIM_ASSERT(bench->Caller(field).Writes() == 0 && bench->Target().Writes(field) == 0);
            GFSIM_ASSERT(bench->Caller(field).MethodThread() == std::thread::id());
        }
        std::thread::id targetThread, callerThreads[2];
        for (unsigned cycle = 1; cycle <= 7; ++cycle) {
            sim.Step();
            const unsigned updates = std::min(cycle, 6u);
            GFSIM_ASSERT(bench->Target().Read(0) == 10 + static_cast<int>(updates));
            GFSIM_ASSERT(bench->Target().Read(1) == 20 + 2 * static_cast<int>((updates + 1) / 2));
            GFSIM_ASSERT(bench->Target().Writes(0) == updates);
            GFSIM_ASSERT(bench->Target().Writes(1) == (updates + 1) / 2);
            GFSIM_ASSERT(bench->Target().WorkCycles() == cycle) << "no implicit extra target Work";
            GFSIM_ASSERT(bench->Target().WorkThread() == bench->Target().XferThread());
            GFSIM_ASSERT((bench->Target().WorkThread() == std::this_thread::get_id()) == (workers == 0));
            if (cycle == 1) targetThread = bench->Target().WorkThread();
            GFSIM_ASSERT(bench->Target().WorkThread() == targetThread);
            for (unsigned field = 0; field < 2; ++field) {
                const auto& caller = bench->Caller(field);
                GFSIM_ASSERT(caller.Observations().size() == updates);
                if (cycle <= 6) {
                    const int expected = field == 0 ? 10 + cycle - 1 : 20 + 2 * (cycle / 2);
                    GFSIM_ASSERT(caller.Observations().back() == expected) << "next-cycle visibility";
                }
                GFSIM_ASSERT(caller.WorkThread() == caller.XferThread());
                GFSIM_ASSERT(caller.MethodThread() == caller.WorkThread());
                GFSIM_ASSERT((caller.WorkThread() == std::this_thread::get_id()) == (workers == 0));
                if (cycle == 1) callerThreads[field] = caller.WorkThread();
                GFSIM_ASSERT(caller.WorkThread() == callerThreads[field]);
                values.push_back(bench->Target().Read(field));
            }
            // Child assignment is unchanged: target, first caller, second caller.
            if (workers >= 2) GFSIM_ASSERT(targetThread != callerThreads[0]);
            if (workers == 4) GFSIM_ASSERT(targetThread != callerThreads[1]);
            GFSIM_ASSERT((callerThreads[0] == callerThreads[1]) == (workers < 2));
            GFSIM_ASSERT(sim.NeedTerminate() == (cycle == 7));
            GFSIM_ASSERT(sim.StalledCycles() == 0);
        }
        GFSIM_ASSERT(bench->OutputsChecked() == 14 && bench->StimuliSent() == 0);
        GFSIM_ASSERT(sim.TerminationReason() == "method_bench complete");
        sim.ReportStat();
        GFSIM_ASSERT(report.str().find("First writes (method calls since reset)") != std::string::npos);
        GFSIM_ASSERT(report.str().find("second_caller writes (method calls since reset)") != std::string::npos);
        GFSIM_ASSERT(log.str().find("[C:1][first_caller.method][DEBUG] staged=11") != std::string::npos);
        GFSIM_ASSERT(log.str().find("[C:2][second_caller.hold][DEBUG] current=22") != std::string::npos);
        GFSIM_ASSERT(log.str().find("[C:7][method_bench.complete][INFO]") != std::string::npos);
        sim.Reset();
        GFSIM_ASSERT(!sim.NeedTerminate() && sim.Cycles() == 0);
    }
    const auto midpoint = values.begin() + values.size() / 2;
    GFSIM_ASSERT(std::equal(values.begin(), midpoint, midpoint)) << "reset replay";
    return values;
}

void NoAutomaticCalls()
{
    SimSystem sim;
    auto target = std::make_shared<MethodTarget>(&sim);
    sim.AddModule(target);
    sim.Build();
    for (unsigned cycle = 0; cycle < 3; ++cycle) sim.Step();
    GFSIM_ASSERT(target->WorkCycles() == 3);
    GFSIM_ASSERT(target->Writes(0) == 0 && target->Writes(1) == 0);
    GFSIM_ASSERT(target->Read(0) == 10 && target->Read(1) == 20);
}

void Deadlock(std::size_t workers)
{
    SimSystem sim;
    auto bench = std::make_shared<MethodBench>(&sim, false);
    sim.AddModule(bench);
    if (workers) {
        sim.SetWorkerCount(workers);
        sim.SetParallelChildren(bench.get());
    }
    sim.SetDeadlockThreshold(2);
    sim.Build();
    for (unsigned pass = 0; pass < 2; ++pass) {
        bool deadlocked = false;
        try { sim.Run(3); }
        catch (const DeadlockError&) { deadlocked = true; }
        GFSIM_ASSERT(deadlocked && sim.IsDeadlocked() && sim.Cycles() == 2);
        GFSIM_ASSERT(!sim.NeedTerminate() && bench->OutputsChecked() == 0);
        GFSIM_ASSERT(bench->Target().Writes(0) == 0 && bench->Target().Writes(1) == 0);
        GFSIM_ASSERT(bench->Target().Read(0) == 10 && bench->Target().Read(1) == 20);
        sim.Reset();
        GFSIM_ASSERT(!sim.IsDeadlocked() && sim.StalledCycles() == 0);
    }
}

int main(int argc, char** argv)
{
    GFSIM_ASSERT(argc == 2) << "expected DOT output path";
    const auto serial = Run(0, argv[1]);
    for (std::size_t workers : {1u, 2u, 4u}) GFSIM_ASSERT(Run(workers, "") == serial);
    NoAutomaticCalls();
    for (std::size_t workers : {0u, 1u, 2u, 4u}) Deadlock(workers);
}
