#include "Counter.h"

namespace counter_example {

Counter::Counter(gfsim::SimQueue<Add>* requests, gfsim::SimQueue<Add>* replies)
    : SimModule("counter"), requests_(BindInput(requests)), replies_(BindOutput(replies)) {}

void Counter::Build()
{
    SetObjectCapacity(0);
    BuildInnerQueues();
    BuildChildren();
}

void Counter::Work()
{
    if (requests_->Empty()) return;
    if (replies_->Full()) {
        Log().Debug(Name(), "stall") << "reply queue full";
        return;
    }

    const auto packet = requests_->Front();
    replies_->Write(packet);
    packet->before = value_;
    nextValue_ = value_ + packet->delta;
    requests_->Pop();

    ++accepted_;
    GetSim()->MarkProgress();
    Log().Info(Name(), "accept") << value_ << " + " << packet->delta << " -> " << nextValue_;
}

void Counter::Xfer()
{
    value_ = nextValue_;
}

void Counter::Reset()
{
    value_ = nextValue_ = accepted_ = 0;
}

void Counter::ReportStat()
{
    Report().ReportVal("Add requests accepted since reset", accepted_);
    Report().ReportVal("Committed counter value", value_);
}

} // namespace counter_example
