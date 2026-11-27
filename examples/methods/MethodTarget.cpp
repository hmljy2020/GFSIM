#include "MethodTarget.h"
#include "SimAssert.h"

namespace method_example {

MethodTarget::MethodTarget(gfsim::SimSystem* system)
    : SimModule("target", system), first_("target.first", 10),
      second_("target.second", 20) {}

void MethodTarget::Build()
{
    SetObjectCapacity(0);
    first_.Build();
    second_.Build();
    BuildInnerQueues();
    BuildChildren();
}

int MethodTarget::Read(unsigned field) const
{
    GFSIM_ASSERT(field < 2) << "method field index";
    return field == 0 ? first_.Read() : second_.Read();
}

std::thread::id MethodTarget::Write(unsigned field, int value)
{
    GFSIM_ASSERT(field < 2) << "method field index";
    (field == 0 ? first_ : second_).Write(value);
    ++writes_[field];
    return std::this_thread::get_id();
}

unsigned MethodTarget::Writes(unsigned field) const
{
    GFSIM_ASSERT(field < 2) << "method field index";
    return writes_[field];
}

void MethodTarget::Work()
{
    workThread_ = std::this_thread::get_id();
    ++workCycles_; // does not implicitly call Write or count as useful progress
    WorkChildren();
}

void MethodTarget::Xfer()
{
    xferThread_ = std::this_thread::get_id();
    first_.Xfer();
    second_.Xfer();
    AdvanceInnerQueues();
    XferChildren();
}

void MethodTarget::Reset()
{
    first_.Reset();
    second_.Reset();
    writes_[0] = writes_[1] = workCycles_ = 0;
    workThread_ = xferThread_ = std::thread::id();
    ResetInnerQueues();
    ResetChildren();
}

void MethodTarget::ReportStat()
{
    Report().ReportVal("Target Work calls (cycles since reset)", workCycles_);
    Report().ReportVal("First writes (method calls since reset)", writes_[0]);
    Report().ReportVal("Second writes (method calls since reset)", writes_[1]);
    Report().ReportVal("First committed value (units)", first_.Read());
    Report().ReportVal("Second committed value (units)", second_.Read());
    first_.ReportStat();
    second_.ReportStat();
    ReportChildren();
}

} // namespace method_example
