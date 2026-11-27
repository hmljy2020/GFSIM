#pragma once

#include "SimModule.h"

namespace method_test {

// Module-template fixture for reference topology, with an optional queue self-loop.
class ReferenceNode final : public gfsim::SimModule {
public:
    ReferenceNode(std::string name, gfsim::SimSystem* system = nullptr,
                  const ReferenceNode* optionalTarget = nullptr,
                  std::vector<std::shared_ptr<ReferenceNode>> children = {},
                  bool queueLoop = false);
    using gfsim::SimModule::BindModule; // expose protected API for contract tests
    void Build() override;
    void Work() override;
    void Xfer() override;
    void Reset() override;
    void ReportStat() override;
    const ReferenceNode* OptionalTarget() const { return optionalTarget_; }

private:
    const ReferenceNode* const optionalTarget_;
    const std::vector<std::shared_ptr<ReferenceNode>> children_;
    const bool queueLoop_;
    unsigned workCycles_ = 0;
    INPUT gfsim::SimQueue<int>* input_ = nullptr;
    OUTPUT gfsim::SimQueue<int>* output_ = nullptr;
};

} // namespace method_test
