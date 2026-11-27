#pragma once

#include "MethodCaller.h"
#include "SimTestbench.h"

namespace method_example {

// Uses the SimTestbench lifecycle implementation and the module template's Build pattern.
class MethodBench final : public gfsim::SimTestbench {
public:
    explicit MethodBench(gfsim::SimSystem* system, bool enabled = true);
    void Build() override;
    const MethodTarget& Target() const { return *target_; }
    const MethodCaller& Caller(unsigned field) const;

protected:
    void DriveStimuli() override;
    void CheckOutputs() override;
    bool Done() const override;
    void ResetBench() override;

private:
    const bool enabled_;
    bool done_ = false;
    std::shared_ptr<MethodTarget> target_;
    std::shared_ptr<MethodCaller> callers_[2];
    const MethodTarget* observer_ = nullptr; // const binding, assigned during Build

    PURE static int Expected(unsigned field, unsigned completedCycles)
    { return field == 0 ? 10 + completedCycles : 20 + 2 * ((completedCycles + 1) / 2); }
};

} // namespace method_example
