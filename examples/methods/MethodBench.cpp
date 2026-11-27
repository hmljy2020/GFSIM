#include "MethodBench.h"

namespace method_example {

MethodBench::MethodBench(gfsim::SimSystem* system, bool enabled)
    : SimTestbench("method_bench", system), enabled_(enabled) {}

void MethodBench::Build()
{
    SetObjectCapacity(3); // references and embedded states add no objects
    target_ = std::make_shared<MethodTarget>();
    observer_ = BindModule(static_cast<const MethodTarget*>(target_.get()));
    callers_[0] = std::make_shared<MethodCaller>("first_caller", target_.get(), 0, enabled_);
    callers_[1] = std::make_shared<MethodCaller>("second_caller", target_.get(), 1, enabled_);
    AddSubmodule(target_);
    for (const auto& caller : callers_) AddSubmodule(caller);
    BuildInnerQueues();
    BuildChildren();
}

const MethodCaller& MethodBench::Caller(unsigned field) const
{
    GFSIM_ASSERT(field < 2) << "caller index";
    return *callers_[field];
}

void MethodBench::DriveStimuli() {} // callers issue writes; bench records only verified outputs

void MethodBench::CheckOutputs()
{
    if (!enabled_) return;
    const unsigned completed = static_cast<unsigned>(GetSim()->Cycles() - 1);
    for (unsigned field = 0; field < 2; ++field)
        ExpectEqual(observer_->Read(field), Expected(field, completed), "committed field");
    done_ = completed == 6;
    if (done_) Log().Info(Name(), "complete") << "verified six update cycles";
}

bool MethodBench::Done() const { return done_; }
void MethodBench::ResetBench() { done_ = false; }

} // namespace method_example
