#pragma once

#include "SimModule.h"

namespace counter_example {

// The client creates this packet. The counter fills in 'before' and returns it.
struct Add {
    unsigned delta = 0;
    unsigned before = 0;
};

class Counter final : public gfsim::SimModule {
public:
    Counter(gfsim::SimQueue<Add>* requests, gfsim::SimQueue<Add>* replies);
    void Build() override;
    void Work() override;
    void Xfer() override;
    void Reset() override;
    void ReportStat() override;

private:
    INPUT gfsim::SimQueue<Add>* requests_;
    OUTPUT gfsim::SimQueue<Add>* replies_;
    unsigned value_ = 0;
    unsigned nextValue_ = 0;
    unsigned accepted_ = 0;
};

} // namespace counter_example
