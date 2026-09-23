#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "SimModule.h"

namespace {

struct Packet {
    explicit Packet(int initial) : value(initial) {}
    int value;
};

void Check(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class Relay final : public gfsim::SimModule {
public:
    Relay(std::string name, gfsim::SimQueue<Packet>* input,
          gfsim::SimQueue<Packet>* output)
        : SimModule(std::move(name)), input_(BindInput(input)),
          output_(BindOutput(output)) {}
    void Build() override {}
    void Work() override
    {
        if (!input_->Empty() && !output_->Full()) {
            output_->Write(input_->Front());
            input_->Pop();
            GetSim()->MarkProgress();
        }
    }
    void Xfer() override {}
    void Reset() override {}
    void ReportStat() override {}

private:
    INPUT gfsim::SimQueue<Packet>* input_;
    OUTPUT gfsim::SimQueue<Packet>* output_;
};

class EmptyModule final : public gfsim::SimModule {
public:
    EmptyModule() : SimModule("extra") {}
    void Build() override {}
    void Work() override {}
    void Xfer() override {}
    void Reset() override {}
    void ReportStat() override {}
};

class CycleRoot final : public gfsim::SimModule {
public:
    explicit CycleRoot(gfsim::SimSystem* system) : SimModule("cycle", system) {}
    void Build() override
    {
        SetObjectCapacity(4);
        auto* ab = AddInnerQueue(std::make_shared<gfsim::SimQueue<Packet>>("a_to_b"));
        auto* ba = AddInnerQueue(std::make_shared<gfsim::SimQueue<Packet>>("b_to_a"));
        AddSubmodule(std::make_shared<Relay>("A", ba, ab));
        AddSubmodule(std::make_shared<Relay>("B", ab, ba));
        BuildInnerQueues();
        BuildChildren();
    }
    void Work() override { WorkChildren(); }
    void Xfer() override { AdvanceInnerQueues(); XferChildren(); }
    void Reset() override { ResetInnerQueues(); ResetChildren(); }
    void ReportStat() override { ReportChildren(); }
};

class DanglingQueueRoot final : public gfsim::SimModule {
public:
    explicit DanglingQueueRoot(gfsim::SimSystem* system)
        : SimModule("dangling", system) {}
    void Build() override
    {
        SetObjectCapacity(1);
        AddInnerQueue(std::make_shared<gfsim::SimQueue<Packet>>("unused"));
        BuildInnerQueues();
    }
    void Work() override {}
    void Xfer() override { AdvanceInnerQueues(); }
    void Reset() override { ResetInnerQueues(); }
    void ReportStat() override {}
};

class SelfLoopRoot final : public gfsim::SimModule {
public:
    explicit SelfLoopRoot(gfsim::SimSystem* system) : SimModule("self_loop", system) {}
    void Build() override
    {
        SetObjectCapacity(1);
        auto* queue = AddInnerQueue(std::make_shared<gfsim::SimQueue<int>>("feedback"));
        input_ = BindInput(queue);
        output_ = BindOutput(queue);
        BuildInnerQueues();
    }
    void Work() override
    {
        if (input_->Empty()) {
            if (output_->Full()) return;
            output_->Write(1);
        } else if (!output_->Full()) {
            const int value = input_->Front();
            output_->Write(value + 1);
            input_->Pop();
        } else {
            return;
        }
        GetSim()->MarkProgress();
    }
    void Xfer() override { AdvanceInnerQueues(); }
    void Reset() override { ResetInnerQueues(); }
    void ReportStat() override {}

private:
    INPUT gfsim::SimQueue<int>* input_ = nullptr;
    OUTPUT gfsim::SimQueue<int>* output_ = nullptr;
};

class TwoProducerRoot final : public gfsim::SimModule {
public:
    explicit TwoProducerRoot(gfsim::SimSystem* system)
        : SimModule("two_producers", system) {}
    void Build() override
    {
        SetObjectCapacity(2);
        auto* queue = AddInnerQueue(std::make_shared<gfsim::SimQueue<Packet>>("shared"));
        BindOutput(queue);
        AddSubmodule(std::make_shared<Relay>("loop", queue, queue));
        BuildInnerQueues();
        BuildChildren();
    }
    void Work() override { WorkChildren(); }
    void Xfer() override { AdvanceInnerQueues(); XferChildren(); }
    void Reset() override { ResetInnerQueues(); ResetChildren(); }
    void ReportStat() override { ReportChildren(); }
};

class RogueOwner final : public gfsim::SimObj {
public:
    RogueOwner() : SimObj("rogue") {}
    void Build() override
    {
        SetObjectCapacity(1);
        queue_ = std::make_shared<gfsim::SimQueue<int>>("unowned");
        AddObject(queue_.get());
        queue_->Build();
    }
    void Work() override {}
    void Xfer() override {}
    void Reset() override { queue_->Reset(); }
    void ReportStat() override {}

private:
    std::shared_ptr<gfsim::SimQueue<int>> queue_;
};

void TestFixedCycle()
{
    gfsim::SimSys system;
    auto root = std::make_shared<CycleRoot>(&system);
    system.AddModule(root);
    system.Build();
    Check(root->Objects().size() == 4 && root->Submodules().size() == 2,
          "Build must fix the direct SimObj collection at four objects");
    bool rejected = false;
    try {
        root->AddSubmodule(std::make_shared<EmptyModule>());
    } catch (const std::logic_error&) {
        rejected = true;
    }
    Check(rejected, "frozen SimObj collection must reject expansion");

    gfsim::SimSys selfLoopSystem;
    selfLoopSystem.AddModule(std::make_shared<SelfLoopRoot>(&selfLoopSystem));
    selfLoopSystem.Build();
    selfLoopSystem.Step();
    selfLoopSystem.Step();
    Check(selfLoopSystem.Cycles() == 2,
          "one module may consume and produce through its own queue");
}

void TestInvalidTopology()
{
    gfsim::SimSys rootQueueSystem;
    bool rejected = false;
    try {
        rootQueueSystem.AddModule(std::make_shared<gfsim::SimQueue<int>>("root_queue"));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Check(rejected, "SimSystem must reject a physical root queue");

    gfsim::SimSys danglingSystem;
    danglingSystem.AddModule(std::make_shared<DanglingQueueRoot>(&danglingSystem));
    rejected = false;
    try {
        danglingSystem.Build();
    } catch (const std::logic_error&) {
        rejected = true;
    }
    Check(rejected, "unconnected queue must fail Build");

    gfsim::SimSys producerSystem;
    producerSystem.AddModule(std::make_shared<TwoProducerRoot>(&producerSystem));
    rejected = false;
    try {
        producerSystem.Build();
    } catch (const std::logic_error&) {
        rejected = true;
    }
    Check(rejected, "queue with two output ports must fail Build");

    gfsim::SimSys rogueSystem;
    rogueSystem.AddModule(std::make_shared<RogueOwner>());
    rejected = false;
    try {
        rogueSystem.Build();
    } catch (const std::logic_error&) {
        rejected = true;
    }
    Check(rejected, "a physical queue outside a parent inner collection must fail Build");
}

void TestPayloadAndCapacity()
{
    gfsim::SimQueue<int> integers("integers");
    gfsim::SimQueue<bool> flags("flags");
    Check(integers.MaxSize() == 512, "default capacity must be 512");
    integers.Write(7);
    flags.Write(true);
    integers.Work();
    flags.Work();
    Check(integers.Read() == 7 && flags.Read(),
          "small scalar payloads must travel directly by value");

    bool rejected = false;
    try {
        integers.InitMaxSize(513);
    } catch (const gfsim::AssertionError&) {
        rejected = true;
    }
    Check(rejected, "queue capacity above 512 must fail");

    gfsim::SimQueue<Packet> packets("packets");
    auto packet = std::make_shared<Packet>(9);
    packets.Write(packet);
    packets.Work();
    Check(packets.Read() == packet,
          "class payloads must preserve shared pointer identity");
}

} // namespace

int main()
{
    try {
        TestFixedCycle();
        TestInvalidTopology();
        TestPayloadAndCapacity();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
