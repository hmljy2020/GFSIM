#include "QueueBench.h"

namespace queue_test {
QueueBench::QueueBench(gfsim::SimSystem* system, bool blocked)
    : SimTestbench("queue_bench", system), blocked_(blocked) {}
void QueueBench::Build()
{
    SetObjectCapacity(6); // two queues and four child modules
    input_ = BindOutput(AddInnerQueue(std::make_shared<gfsim::SimQueue<ImmutablePacket>>("input")));
    output_ = BindInput(AddInnerQueue(std::make_shared<gfsim::SimQueue<ImmutablePacket>>("output")));
    input_->InitMaxSize(3);
    input_->Setlatency(2);
    output_->InitMaxSize(1);
    for (auto* queue : {input_, output_}) {
        queue->SetTraceFormatter([](const ImmutablePacket& p) { return std::to_string(p.value); });
    }
    forwarder_ = std::make_shared<QueueView>("consumer", input_, output_);
    AddSubmodule(std::make_shared<QueueView>("before", nullptr, nullptr, forwarder_.get()));
    AddSubmodule(forwarder_);
    AddSubmodule(std::make_shared<QueueView>("after", nullptr, nullptr, forwarder_.get()));
    AddSubmodule(std::make_shared<QueueView>("extra", nullptr, nullptr, forwarder_.get()));
    BuildInnerQueues();
    BuildChildren();
}
void QueueBench::DriveStimuli()
{
    if (sent_.size() == 6 || input_->Full()) return;
    auto packet = std::make_shared<ImmutablePacket>(static_cast<unsigned>(sent_.size()));
    input_->Write(packet);
    sent_.push_back(packet);
    RecordStimulus("immutable packet");
}
void QueueBench::CheckOutputs()
{
    if (blocked_ || GetSim()->Cycles() < 8 || output_->Empty()) return;
    const auto packet = output_->Front();
    const auto index = OutputsChecked();
    GFSIM_ASSERT(packet == sent_.at(index) && packet->value == index);
    output_->Pop();
    RecordOutputChecked("immutable packet identity and value");
}
bool QueueBench::Done() const { return OutputsChecked() == 6; }
void QueueBench::ResetBench() { sent_.clear(); }
}
