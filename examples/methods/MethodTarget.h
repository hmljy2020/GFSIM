#pragma once

#include <thread>
#include "SimModule.h"
#include "SimState.h"

namespace method_example {

// Adapted from templates/SimModule.h.in: no queue ports or owned objects.
class MethodTarget final : public gfsim::SimModule {
public:
    explicit MethodTarget(gfsim::SimSystem* system = nullptr);
    void Build() override;
    void Work() override;
    void Xfer() override;
    void Reset() override;
    void ReportStat() override;

    int Read(unsigned field) const;
    std::thread::id Write(unsigned field, int value);
    // Diagnostics are inspected only with workers idle (or after WorkChildren).
    unsigned Writes(unsigned field) const;
    unsigned WorkCycles() const { return workCycles_; }
    std::thread::id WorkThread() const { return workThread_; }
    std::thread::id XferThread() const { return xferThread_; }

private:
    gfsim::SimState<int> first_;
    gfsim::SimState<int> second_;
    unsigned writes_[2] = {0, 0}; // each field has exactly one caller/writer
    unsigned workCycles_ = 0;
    std::thread::id workThread_, xferThread_;
};

} // namespace method_example
