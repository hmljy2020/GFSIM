#pragma once

#include <thread>
#include <vector>
#include "SimModule.h"
#include "SimState.h"

namespace state_test {

// Adapted from templates/SimModule.{h,cpp,md}.in; no queue ports are needed.
class StateAccess final : public gfsim::SimModule {
public:
    enum class Field { Pc, Count };
    struct Observation {
        std::uint64_t pc;
        std::uint64_t count;
    };
    StateAccess(std::string name, gfsim::SimState<std::uint64_t>* pc,
                gfsim::SimState<std::uint64_t>* count, Field field, bool enabled);
    void Build() override;
    void Work() override;
    void Xfer() override;
    void Reset() override;
    void ReportStat() override;

    const std::vector<Observation>& Observations() const { return observations_; }
    std::thread::id WorkThread() const { return workThread_; }
    std::thread::id XferThread() const { return xferThread_; }

private:
    PURE static std::uint64_t NextPc(std::uint64_t pc, std::uint64_t count)
    { return pc + count + 1; }
    PURE static std::uint64_t NextCount(std::uint64_t pc, std::uint64_t count)
    { return count + pc + 2; }
    void AccessState();

    gfsim::SimState<std::uint64_t>* pc_; // non-owning; owner outlives access modules
    gfsim::SimState<std::uint64_t>* count_;
    Field field_;
    bool enabled_;
    std::vector<Observation> observations_;
    std::thread::id workThread_;
    std::thread::id xferThread_;
};

} // namespace state_test
