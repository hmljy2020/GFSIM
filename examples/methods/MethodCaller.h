#pragma once

#include <thread>
#include <vector>
#include "MethodTarget.h"

namespace method_example {

class MethodCaller final : public gfsim::SimModule {
public:
    MethodCaller(std::string name, MethodTarget* target, unsigned field, bool enabled);
    void Build() override;
    void Work() override;
    void Xfer() override;
    void Reset() override;
    void ReportStat() override;

    const std::vector<int>& Observations() const { return observations_; }
    std::thread::id WorkThread() const { return workThread_; }
    std::thread::id XferThread() const { return xferThread_; }
    std::thread::id MethodThread() const { return methodThread_; }
    unsigned Writes() const { return writes_; }

private:
    MethodTarget* const target_; // non-owning, registered with BindModule
    const unsigned field_;
    const bool enabled_;
    std::vector<int> observations_;
    unsigned writes_ = 0;
    std::thread::id workThread_, xferThread_, methodThread_;

    PURE static int NextValue(int current, unsigned field)
    { return current + static_cast<int>(field) + 1; }
    void CallTarget();
};

} // namespace method_example
