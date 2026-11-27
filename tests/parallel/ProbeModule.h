#pragma once
#include <functional>
#include "SimModule.h"
#include "RingStage.h"

namespace parallel_test {
// Adapted from templates/SimModule.{h,cpp,md}.in; intentionally has no ports.
class ProbeModule final : public gfsim::SimModule {
public:
    using Hook = std::function<void(ProbeModule&, bool, bool)>;
    ProbeModule(std::string name, gfsim::SimSystem* system = nullptr,
                std::vector<std::shared_ptr<gfsim::SimModule>> children = {},
                std::vector<std::shared_ptr<gfsim::SimQueue<RingPacket>>> queues = {});
    void Build() override;
    void Work() override;
    void Xfer() override;
    void Reset() override;
    void ReportStat() override;
    void SetHook(Hook hook) { hook_ = std::move(hook); }
    void SetResetHook(std::function<void()> hook) { resetHook_ = std::move(hook); }
    std::uint64_t Works() const { return works_; }
    std::uint64_t Xfers() const { return xfers_; }
private:
    void Observe(bool work, bool after);
    std::vector<std::shared_ptr<gfsim::SimModule>> children_;
    std::vector<std::shared_ptr<gfsim::SimQueue<RingPacket>>> queues_;
    Hook hook_;
    std::function<void()> resetHook_;
    std::uint64_t works_ = 0;
    std::uint64_t xfers_ = 0;
};
}
