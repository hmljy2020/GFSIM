#pragma once

#include "SimModule.h"

namespace counter_example {

class CounterSystem final : public gfsim::SimModule {
public:
    explicit CounterSystem(gfsim::SimSystem* system);
    void Build() override;
    void Work() override;
    void Xfer() override;
    void Reset() override;
    void ReportStat() override;
};

} // namespace counter_example
