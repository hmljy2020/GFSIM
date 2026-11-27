#include "RingStage.h"
namespace parallel_test {
RingStage::RingStage(std::string name, gfsim::SimQueue<RingPacket>* input,
                     std::shared_ptr<gfsim::SimQueue<RingPacket>> output, bool ownOutput)
    : SimModule(std::move(name)), input_(BindInput(input)),
      output_(BindOutput(output.get())), owned_(ownOutput ? std::move(output) : nullptr) {}
void RingStage::Build()
{
    SetObjectCapacity(owned_ ? 1 : 0);
    if (owned_) AddInnerQueue(owned_);
    output_->InitMaxSize(1);
    output_->Setlatency(2);
    output_->SetTraceFormatter([](const RingPacket& packet) {
        return std::to_string(packet.id) + ":" + std::to_string(packet.hops);
    });
    BuildInnerQueues();
    BuildChildren();
}
void RingStage::Transfer()
{
    if (input_->Empty()) return;
    const auto packet = input_->Front();
    if (packet->id == 99 || packet->hops == 4) {
        input_->Pop();
        GetSim()->MarkProgress();
        if (packet->hops == 4) GetSim()->RequestTerminate("ring complete");
        return;
    }
    if (output_->Full()) {
        ++blocked_;
        Log().Debug(Name(), "blocked") << "output_full";
        return;
    }
    const auto hops = NextHop(input_->Peek().hops);
    output_->Write(packet);
    input_->Pop();
    packet->hops = hops;
    ++transfers_;
    GetSim()->MarkProgress();
    Log().Debug(Name(), "forward") << "hops=" << hops;
}
void RingStage::Work()
{
    workThread_ = std::this_thread::get_id();
    Transfer();
    WorkChildren();
}
void RingStage::Xfer() { AdvanceInnerQueues(); XferChildren(); }
void RingStage::Reset()
{
    transfers_ = blocked_ = 0;
    workThread_ = std::thread::id();
    ResetInnerQueues();
    ResetChildren();
}
void RingStage::ReportStat()
{
    Report().ReportVal(Name() + " forwarded packets (since reset)", transfers_);
    Report().ReportVal(Name() + " blocked cycles (since reset)", blocked_);
    ReportChildren();
}
}
