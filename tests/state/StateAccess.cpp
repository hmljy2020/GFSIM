#include "StateAccess.h"
#include "SimAssert.h"

namespace state_test {

StateAccess::StateAccess(std::string name, gfsim::SimState<std::uint64_t>* pc,
                         gfsim::SimState<std::uint64_t>* count,
                         Field field, bool enabled)
    : SimModule(std::move(name)), pc_(pc), count_(count), field_(field), enabled_(enabled)
{
    GFSIM_ASSERT(pc_ != nullptr && count_ != nullptr) << "state access requires an owner";
}

void StateAccess::Build()
{
    SetObjectCapacity(0);
    BuildInnerQueues();
    BuildChildren();
}

void StateAccess::AccessState()
{
    const Observation prior{pc_->Read(), count_->Read()};
    if (enabled_) {
        if (field_ == Field::Pc) pc_->Write(NextPc(prior.pc, prior.count));
        else count_->Write(NextCount(prior.pc, prior.count));
        GetSim()->MarkProgress(); // one useful field update staged
        Log().Debug(Name(), "work") << "staged field update";
    } else {
        Log().Debug(Name(), "stall") << "field updates disabled";
    }
    GFSIM_ASSERT(pc_->Read() == prior.pc && count_->Read() == prior.count)
        << "current changed during Work";
    observations_.push_back(prior); // only this worker writes its observations
}

void StateAccess::Work()
{
    workThread_ = std::this_thread::get_id();
    AccessState();
    WorkChildren();
}

void StateAccess::Xfer()
{
    xferThread_ = std::this_thread::get_id();
    // The referenced state belongs to the owner; never access it during Xfer.
    AdvanceInnerQueues();
    XferChildren();
}

void StateAccess::Reset()
{
    observations_.clear();
    workThread_ = xferThread_ = std::thread::id();
    ResetInnerQueues();
    ResetChildren();
}

void StateAccess::ReportStat()
{
    Report().ReportVal(Name() + " current snapshots (observations since reset)",
                       observations_.size());
    ReportChildren();
}

} // namespace state_test
