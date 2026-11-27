#include "MethodCaller.h"
#include "SimAssert.h"

namespace method_example {

MethodCaller::MethodCaller(std::string name, MethodTarget* target, unsigned field, bool enabled)
    : SimModule(std::move(name)), target_(BindModule(target)), field_(field), enabled_(enabled)
{
    GFSIM_ASSERT(field < 2) << "one caller per field";
}

void MethodCaller::Build()
{
    SetObjectCapacity(0);
    BuildInnerQueues();
    BuildChildren();
}

void MethodCaller::CallTarget()
{
    const std::uint64_t cycle = GetSim()->Cycles();
    if (!enabled_ || cycle > 6) return;
    const int before = target_->Read(field_);
    observations_.push_back(before);
    // Leave the second state unwritten on even cycles to demonstrate holding.
    if (field_ == 1 && cycle % 2 == 0) {
        Log().Debug(Name(), "hold") << "current=" << before;
        return;
    }
    methodThread_ = target_->Write(field_, NextValue(before, field_));
    GFSIM_ASSERT(methodThread_ == std::this_thread::get_id()) << "synchronous method call";
    GFSIM_ASSERT(target_->Read(field_) == before) << "write remains pending this cycle";
    ++writes_;
    GetSim()->MarkProgress();
    Log().Debug(Name(), "method") << "staged=" << NextValue(before, field_);
}

void MethodCaller::Work()
{
    workThread_ = std::this_thread::get_id();
    CallTarget();
    WorkChildren();
}

void MethodCaller::Xfer()
{
    xferThread_ = std::this_thread::get_id();
    AdvanceInnerQueues();
    XferChildren();
}

void MethodCaller::Reset()
{
    observations_.clear();
    writes_ = 0;
    workThread_ = xferThread_ = methodThread_ = std::thread::id();
    ResetInnerQueues();
    ResetChildren();
}

void MethodCaller::ReportStat()
{
    Report().ReportVal(Name() + " writes (method calls since reset)", writes_);
    Report().ReportVal(Name() + " observations (active cycles since reset)", observations_.size());
    ReportChildren();
}

} // namespace method_example
