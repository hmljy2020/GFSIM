#pragma once
#include <thread>
#include "SimModule.h"
namespace parallel_test {
struct RingPacket { int id; unsigned hops; };
// Adapted from the module template. Each stage owns its outgoing physical queue.
class RingStage final : public gfsim::SimModule {
public:
    RingStage(std::string name, gfsim::SimQueue<RingPacket>* input,
              std::shared_ptr<gfsim::SimQueue<RingPacket>> output, bool ownOutput = true);
    void Build() override;
    void Work() override;
    void Xfer() override;
    void Reset() override;
    void ReportStat() override;
    std::thread::id WorkThread() const { return workThread_; }
private:
    PURE static unsigned NextHop(unsigned hops) { return hops + 1; }
    void Transfer();
    INPUT gfsim::SimQueue<RingPacket>* input_;
    OUTPUT gfsim::SimQueue<RingPacket>* output_;
    std::shared_ptr<gfsim::SimQueue<RingPacket>> owned_;
    std::uint64_t transfers_ = 0;
    std::uint64_t blocked_ = 0;
    std::thread::id workThread_;
};
}
