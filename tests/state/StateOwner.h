#pragma once

#include "SimModule.h"
#include "StateAccess.h"
#include "SimState.h"

namespace state_test {

// Adapted from templates/SimModule.{h,cpp,md}.in; zero ports, two children.
class StateOwner final : public gfsim::SimModule {
public:
    explicit StateOwner(gfsim::SimSystem* system, std::uint64_t completionCycles = 32,
                        bool enabled = true);
    void Build() override;
    void Work() override;
    void Xfer() override;
    void Reset() override;
    void ReportStat() override;

    const gfsim::SimState<std::uint64_t>& Pc() const { return pc_; }
    const gfsim::SimState<std::uint64_t>& Count() const { return count_; }
    const gfsim::SimState<std::uint64_t>& Auxiliary() const { return auxiliary_; }
    const gfsim::SimState<std::uint64_t>& BankPc(std::size_t i) const;
    const gfsim::SimState<std::uint32_t>& BankCount(std::size_t i) const;
    const StateAccess& Access(std::size_t i) const;

private:
    PURE static std::uint64_t NextCount(std::uint64_t count) { return count + 1; }
    void UpdateState();

    gfsim::SimState<std::uint64_t> pc_;
    gfsim::SimState<std::uint64_t> count_;
    gfsim::SimState<std::uint64_t> auxiliary_;
    gfsim::SimState<std::uint64_t> bankPc_[2];
    gfsim::SimState<std::uint32_t> bankCount_[2];
    std::shared_ptr<StateAccess> access_[2];
    std::uint64_t completionCycles_;
    bool enabled_;
};

} // namespace state_test
