#include <sstream>
#include <vector>
#include "QueueBench.h"

using namespace gfsim;
using namespace queue_test;

namespace {
std::vector<unsigned> Run(unsigned workers, const char* dotPath)
{
    SimSystem sim;
    auto bench = std::make_shared<QueueBench>(&sim);
    sim.AddModule(bench);
    if (workers) {
        sim.SetWorkerCount(workers);
        sim.SetParallelChildren(bench.get());
    }
    sim.SetDeadlockThreshold(16);
    sim.Build();
    GFSIM_ASSERT(bench->Objects().size() == 6 && bench->Submodules().size() == 4);
    for (const auto& child : bench->Submodules()) {
        GFSIM_ASSERT(child->Parent() == bench.get() && child->GetSim() == &sim);
    }
    if (dotPath) sim.SaveDot(dotPath);
    std::ostringstream log, report;
    sim.GetLogger().SetOutput(log);
    sim.GetLogger().SetLevel(LogLevel::Debug);
    sim.GetReporter().SetOutput(report);
    std::vector<unsigned> expected;
    for (unsigned pass = 0; pass < 2; ++pass) {
        std::vector<unsigned> values;
        while (!sim.NeedTerminate() && sim.Cycles() < 100) {
            const auto before = bench->Forwarder().Inspect(); // quiescent cycle-start view
            sim.Step();
            for (const auto& child : bench->Submodules()) {
                const auto& view = dynamic_cast<const QueueView&>(*child);
                GFSIM_ASSERT(view.Observations().back() == before);
            }
            const auto after = bench->Forwarder().Inspect();
            values.push_back(after.size);
            values.push_back(after.empty ? 99 : after.front->value);
            values.push_back(static_cast<unsigned>(bench->OutputsChecked()));
            if (sim.Cycles() >= 4 && sim.Cycles() <= 8) {
                GFSIM_ASSERT(!after.empty && after.front == bench->Sent().at(1));
                GFSIM_ASSERT(after.front->value == 1) << "blocked input identity and payload";
            }
        }
        GFSIM_ASSERT(sim.NeedTerminate() && sim.TerminationReason() == "queue_bench complete");
        GFSIM_ASSERT(bench->OutputsChecked() == 6 && bench->StimuliSent() == 6);
        GFSIM_ASSERT(bench->Forwarder().Inspect().empty && bench->Forwarder().Blocked() > 0);
        sim.ReportStat();
        GFSIM_ASSERT(log.str().find("[consumer.blocked][DEBUG]") != std::string::npos);
        GFSIM_ASSERT(log.str().find("[consumer.forward][DEBUG]") != std::string::npos);
        GFSIM_ASSERT(report.str().find("consumer forwarded packets (since reset)") != std::string::npos);
        if (pass == 0) expected = values;
        else GFSIM_ASSERT(values == expected) << "reset replay";
        sim.Reset();
        GFSIM_ASSERT(bench->Forwarder().Observations().empty() && bench->Sent().empty());
    }
    return expected;
}
void Deadlock(unsigned workers)
{
    SimSystem sim;
    auto bench = std::make_shared<QueueBench>(&sim, true);
    sim.AddModule(bench);
    if (workers) {
        sim.SetWorkerCount(workers);
        sim.SetParallelChildren(bench.get());
    }
    sim.SetDeadlockThreshold(5);
    sim.Build();
    for (unsigned pass = 0; pass < 2; ++pass) {
        bool deadlocked = false;
        try { sim.Run(100); } catch (const DeadlockError&) { deadlocked = true; }
        GFSIM_ASSERT(deadlocked && sim.IsDeadlocked() && sim.StalledCycles() == 5);
        GFSIM_ASSERT(!sim.NeedTerminate() && bench->OutputsChecked() == 0);
        const auto pending = bench->Forwarder().Inspect();
        GFSIM_ASSERT(pending.front == bench->Sent().at(1) && pending.front->value == 1);
        sim.Reset();
    }
}
}
int main(int argc, char** argv)
{
    GFSIM_ASSERT(argc == 2);
    const auto serial = Run(0, argv[1]);
    for (unsigned workers : {1u, 2u, 4u}) GFSIM_ASSERT(Run(workers, nullptr) == serial);
    for (unsigned workers : {0u, 1u, 2u, 4u}) Deadlock(workers);
}
