#include "ReferenceNode.h"

namespace method_test {

ReferenceNode::ReferenceNode(std::string name, gfsim::SimSystem* system,
                             const ReferenceNode* optionalTarget,
                             std::vector<std::shared_ptr<ReferenceNode>> children,
                             bool queueLoop)
    : SimModule(std::move(name), system),
      optionalTarget_(optionalTarget ? BindModule(optionalTarget) : nullptr),
      children_(std::move(children)), queueLoop_(queueLoop) {}

void ReferenceNode::Build()
{
    SetObjectCapacity(children_.size() + (queueLoop_ ? 1 : 0));
    if (queueLoop_) {
        auto* queue = AddInnerQueue(std::make_shared<gfsim::SimQueue<int>>("feedback"));
        input_ = BindInput(queue);
        output_ = BindOutput(queue);
    }
    for (const auto& child : children_) AddSubmodule(child);
    BuildInnerQueues();
    BuildChildren();
}

void ReferenceNode::Work() { ++workCycles_; WorkChildren(); }
void ReferenceNode::Xfer() { AdvanceInnerQueues(); XferChildren(); }
void ReferenceNode::Reset() { workCycles_ = 0; ResetInnerQueues(); ResetChildren(); }
void ReferenceNode::ReportStat()
{
    Report().ReportVal(Name() + " Work calls (cycles since reset)", workCycles_);
    ReportChildren();
}

} // namespace method_test
