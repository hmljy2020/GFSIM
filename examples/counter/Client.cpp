#include "Client.h"

namespace counter_example {
namespace {
const unsigned deltas[] = {2, 3};
const unsigned expectedBefore[] = {0, 2};
const unsigned requestCount = sizeof(deltas) / sizeof(deltas[0]);
}

Client::Client(gfsim::SimQueue<Add>* requests, gfsim::SimQueue<Add>* replies)
    : SimTestbench("client"), requests_(BindOutput(requests)), replies_(BindInput(replies)) {}

void Client::Build()
{
    SetObjectCapacity(0);
    BuildInnerQueues();
    BuildChildren();
}

void Client::DriveStimuli()
{
    if (StimuliSent() == requestCount || requests_->Full()) return;
    auto packet = std::make_shared<Add>();
    packet->delta = deltas[StimuliSent()];
    requests_->Write(packet);
    RecordStimulus("add");
    Log().Info(Name(), "send") << "delta=" << packet->delta;
}

void Client::CheckOutputs()
{
    if (replies_->Empty()) return;
    const auto& reply = replies_->Peek();
    const auto index = OutputsChecked();
    GFSIM_ASSERT(index < requestCount) << "unexpected reply";
    GFSIM_ASSERT(reply.delta == deltas[index] && reply.before == expectedBefore[index])
        << "reply " << index << ": delta=" << reply.delta << " before=" << reply.before;
    Log().Info(Name(), "reply") << "delta=" << reply.delta << " before=" << reply.before;
    replies_->Pop();
    RecordOutputChecked("add reply");
}

bool Client::Done() const { return OutputsChecked() == requestCount; }
void Client::ResetBench() {} // The base resets both counts.

} // namespace counter_example
