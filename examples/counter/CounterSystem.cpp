#include "CounterSystem.h"
#include "Client.h"

namespace counter_example {
namespace {
std::string FormatAdd(const Add& packet)
{
    return "delta=" + std::to_string(packet.delta) + " before=" + std::to_string(packet.before);
}
}

CounterSystem::CounterSystem(gfsim::SimSystem* system) : SimModule("counter_system", system) {}

void CounterSystem::Build()
{
    SetObjectCapacity(4);
    auto* requests = AddInnerQueue(std::make_shared<gfsim::SimQueue<Add>>("requests"));
    auto* replies = AddInnerQueue(std::make_shared<gfsim::SimQueue<Add>>("replies"));
    requests->InitMaxSize(2);
    replies->InitMaxSize(1);
    requests->SetTraceFormatter(FormatAdd);
    replies->SetTraceFormatter(FormatAdd);
    AddSubmodule(std::make_shared<Counter>(requests, replies));
    AddSubmodule(std::make_shared<Client>(requests, replies));
    BuildInnerQueues();
    BuildChildren();
}

void CounterSystem::Work() { WorkChildren(); }

void CounterSystem::Xfer()
{
    AdvanceInnerQueues();
    XferChildren();
}

void CounterSystem::Reset()
{
    ResetInnerQueues();
    ResetChildren();
}

void CounterSystem::ReportStat()
{
    Report().ReportTitle("Counter exchange");
    ReportChildren();
}

} // namespace counter_example
