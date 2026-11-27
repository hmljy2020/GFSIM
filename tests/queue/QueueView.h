#pragma once
#include <vector>
#include "SimModule.h"

namespace queue_test {
struct ImmutablePacket {
    explicit ImmutablePacket(unsigned v) : value(v) {}
    const unsigned value;
};
struct Observation {
    bool empty;
    unsigned size;
    std::shared_ptr<ImmutablePacket> front;
    bool operator==(const Observation& rhs) const
    { return empty == rhs.empty && size == rhs.size && front == rhs.front; }
};

// Adapted from templates/SimModule.{h,cpp,md}.in. A forwarding instance has
// real input/output ports; an observer instance has only a const method target.
class QueueView final : public gfsim::SimModule {
public:
    QueueView(std::string name, gfsim::SimQueue<ImmutablePacket>* input,
              gfsim::SimQueue<ImmutablePacket>* output, const QueueView* target = nullptr);
    void Build() override;
    void Work() override;
    void Xfer() override;
    void Reset() override;
    void ReportStat() override;
    Observation Inspect() const;
    const std::vector<Observation>& Observations() const { return observations_; }
    unsigned Blocked() const { return blocked_; }
private:
    void Forward();
    INPUT gfsim::SimQueue<ImmutablePacket>* input_;
    OUTPUT gfsim::SimQueue<ImmutablePacket>* output_;
    const QueueView* const target_;
    std::vector<Observation> observations_;
    unsigned forwarded_ = 0;
    unsigned blocked_ = 0;
};
}
