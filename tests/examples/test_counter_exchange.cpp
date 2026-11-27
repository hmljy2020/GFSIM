#include <sstream>
#include "Client.h"
#include "CounterSystem.h"

namespace {
gfsim::SimQueue<counter_example::Add>& Queue(counter_example::CounterSystem& top,
                                             const std::string& name)
{
    for (auto* object : top.Objects()) {
        if (object->Name() == name) {
            return dynamic_cast<gfsim::SimQueue<counter_example::Add>&>(*object);
        }
    }
    throw std::logic_error("missing example queue: " + name);
}
}

int main()
{
    std::ostringstream log, report;
    gfsim::SimSystem sim;
    sim.GetLogger().SetOutput(log);
    sim.GetLogger().SetLevel(gfsim::LogLevel::Info);
    sim.GetReporter().SetOutput(report);
    auto top = std::make_shared<counter_example::CounterSystem>(&sim);
    sim.AddModule(top);
    sim.SetDeadlockThreshold(4);
    sim.Build();
    GFSIM_ASSERT(top->Objects().size() == 4 && top->Submodules().size() == 2);
    for (const auto& child : top->Submodules()) {
        GFSIM_ASSERT(child->Parent() == top.get() && child->GetSim() == &sim);
    }
    auto& requests = Queue(*top, "requests");
    auto& replies = Queue(*top, "replies");
    sim.Step();
    GFSIM_ASSERT(requests.Size() == 1 && replies.Empty());
    const auto first = requests.Front();
    sim.Step();
    GFSIM_ASSERT(replies.Front() == first && first->delta == 2 && first->before == 0);
    const auto second = requests.Front();
    sim.Step();
    GFSIM_ASSERT(requests.Front() == second && second->before == 0 && replies.Empty());
    sim.Step();
    GFSIM_ASSERT(replies.Front() == second && second->before == 2 && requests.Empty());
    sim.Step();
    GFSIM_ASSERT(sim.NeedTerminate() && sim.TerminationReason() == "client complete");
    sim.ReportStat();
    GFSIM_ASSERT(report.str().find("Committed counter value") != std::string::npos);
    GFSIM_ASSERT(log.str().find("2 + 3 -> 5") != std::string::npos);

    sim.Reset();
    replies.setStall();
    sim.Step();
    sim.Step(); // First reply staged, second request visible.
    const auto waiting = requests.Front();
    sim.Step();
    GFSIM_ASSERT(replies.Full() && replies.Empty() && requests.Front() == waiting);
    GFSIM_ASSERT(waiting->delta == 3 && waiting->before == 0);
    replies.unsetStall();
    sim.Step(); // First reply becomes visible.
    sim.Step(); // Client consumes the first reply.
    sim.Step(); // Counter accepts the second request.
    GFSIM_ASSERT(requests.Empty() && replies.Front() == waiting && waiting->before == 2);
    sim.Step();
    GFSIM_ASSERT(sim.NeedTerminate());

    sim.Reset();
    replies.setStall();
    try {
        sim.Run(10);
    } catch (const gfsim::DeadlockError&) {
        GFSIM_ASSERT(sim.IsDeadlocked() && sim.StalledCycles() == 4);
        return 0;
    }
    GFSIM_ASSERT(false) << "expected deadlock with permanently stalled replies";
}
