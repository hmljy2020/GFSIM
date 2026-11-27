#include "ProbeModule.h"
namespace parallel_test {
ProbeModule::ProbeModule(std::string name, gfsim::SimSystem* system,
                         std::vector<std::shared_ptr<gfsim::SimModule>> children,
                         std::vector<std::shared_ptr<gfsim::SimQueue<RingPacket>>> queues)
    : SimModule(std::move(name), system), children_(std::move(children)),
      queues_(std::move(queues)) {}
void ProbeModule::Build()
{
    SetObjectCapacity(children_.size() + queues_.size());
    for (const auto& queue : queues_) AddInnerQueue(queue);
    for (const auto& child : children_) AddSubmodule(child);
    BuildInnerQueues();
    BuildChildren();
}
void ProbeModule::Observe(bool work, bool after)
{
    if (hook_) hook_(*this, work, after);
}
void ProbeModule::Work()
{
    ++works_;
    Observe(true, false);
    Log().Debug(Name(), "work") << "operations=" << works_;
    if (auto* trace = Trace()) {
        trace->AddDuration(Path(), "work", GetSim()->Cycles() - 1, GetSim()->Cycles(), "probe\n\"");
        trace->AddFlow(Path(), Path(), GetSim()->Cycles() - 1, GetSim()->Cycles(), "dependency");
    }
    WorkChildren();
    Observe(true, true);
}
void ProbeModule::Xfer()
{
    ++xfers_;
    Observe(false, false);
    AdvanceInnerQueues();
    XferChildren();
    Observe(false, true);
}
void ProbeModule::Reset()
{
    works_ = xfers_ = 0;
    if (resetHook_) resetHook_();
    ResetInnerQueues();
    ResetChildren();
}
void ProbeModule::ReportStat()
{
    Report().ReportVal(Name() + " Work calls (calls since reset)", works_);
    Report().ReportVal(Name() + " Xfer calls (calls since reset)", xfers_);
    ReportChildren();
}
}
