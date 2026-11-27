#pragma once

#include "Counter.h"
#include "SimTestbench.h"

namespace counter_example {

class Client final : public gfsim::SimTestbench {
public:
    Client(gfsim::SimQueue<Add>* requests, gfsim::SimQueue<Add>* replies);
    void Build() override;

private:
    void DriveStimuli() override;
    void CheckOutputs() override;
    bool Done() const override;
    void ResetBench() override;

    OUTPUT gfsim::SimQueue<Add>* requests_;
    INPUT gfsim::SimQueue<Add>* replies_;
};

} // namespace counter_example
