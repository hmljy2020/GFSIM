#pragma once
#include "QueueView.h"
#include "SimTestbench.h"

namespace queue_test {
// Template lifecycle/ownership pattern with SimTestbench's final traversal.
class QueueBench final : public gfsim::SimTestbench {
public:
    QueueBench(gfsim::SimSystem* system, bool blocked = false);
    void Build() override;
    const QueueView& Forwarder() const { return *forwarder_; }
    const std::vector<std::shared_ptr<ImmutablePacket>>& Sent() const { return sent_; }
protected:
    void DriveStimuli() override;
    void CheckOutputs() override;
    bool Done() const override;
    void ResetBench() override;
private:
    OUTPUT gfsim::SimQueue<ImmutablePacket>* input_ = nullptr;
    INPUT gfsim::SimQueue<ImmutablePacket>* output_ = nullptr;
    std::shared_ptr<QueueView> forwarder_;
    std::vector<std::shared_ptr<ImmutablePacket>> sent_;
    const bool blocked_;
};
}
