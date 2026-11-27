#include "QueueView.h"
#include <thread>

namespace queue_test {
QueueView::QueueView(std::string name, gfsim::SimQueue<ImmutablePacket>* input,
                     gfsim::SimQueue<ImmutablePacket>* output, const QueueView* target)
    : SimModule(std::move(name)), input_(input ? BindInput(input) : nullptr),
      output_(output ? BindOutput(output) : nullptr),
      target_(target ? BindModule(target) : nullptr)
{
    GFSIM_ASSERT((input && output && !target) || (!input && !output && target));
}
void QueueView::Build()
{
    SetObjectCapacity(0);
    BuildInnerQueues();
    BuildChildren();
}
Observation QueueView::Inspect() const
{
    Observation result{input_->Empty(), input_->Size(), nullptr};
    if (!result.empty) {
        result.front = input_->Front();
        GFSIM_ASSERT(input_->Peek().value == result.front->value);
    }
    return result;
}
void QueueView::Forward()
{
    if (input_->Empty()) return;
    if (output_->Full()) {
        ++blocked_;
        Log().Debug(Name(), "blocked") << "output_full";
        return;
    }
    const auto packet = input_->Front();
    output_->Write(packet);
    if (packet->value % 2 == 0) {
        GFSIM_ASSERT(input_->Read() == packet);
    } else {
        input_->Pop();
    }
    ++forwarded_;
    GetSim()->MarkProgress();
    Log().Debug(Name(), "forward") << "value=" << packet->value;
}
void QueueView::Work()
{
    const QueueView* view = target_ ? target_ : this;
    const auto before = view->Inspect();
    if (!target_) Forward();
    // Repeated read-only method calls overlap the consumer's request on workers.
    for (unsigned i = 0; i < 64; ++i) {
        GFSIM_ASSERT(view->Inspect() == before) << "visible structure changed in Work";
        std::this_thread::yield();
    }
    observations_.push_back(before);
    WorkChildren();
}
void QueueView::Xfer() { AdvanceInnerQueues(); XferChildren(); }
void QueueView::Reset()
{
    observations_.clear();
    forwarded_ = blocked_ = 0;
    ResetInnerQueues();
    ResetChildren();
}
void QueueView::ReportStat()
{
    Report().ReportVal(Name() + " forwarded packets (since reset)", forwarded_);
    Report().ReportVal(Name() + " blocked cycles (since reset)", blocked_);
    Report().ReportVal(Name() + " observations (Work cycles since reset)", observations_.size());
    ReportChildren();
}
}
