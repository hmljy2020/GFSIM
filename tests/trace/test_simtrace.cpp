#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include "SimModule.h"
#include "../support/Payload.h"

namespace {

void Check(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::size_t Count(const std::string& text, const std::string& value)
{
    std::size_t count = 0;
    for (std::size_t offset = 0; (offset = text.find(value, offset)) != std::string::npos;
         offset += value.size()) {
        ++count;
    }
    return count;
}

struct Packet {
    int id;
};

class Child final : public gfsim::SimModule {
public:
    Child() : SimModule("child") {}
    void Build() override
    {
        SetObjectCapacity(1);
        queue_ = AddInnerQueue(
            std::make_shared<gfsim::SimQueue<gfsim_test::Number>>("leaf"));
        BindOutput(queue_);
        BindInput(queue_);
        BuildInnerQueues();
    }
    void Work() override {}
    void Xfer() override { AdvanceInnerQueues(); }
    void Reset() override { ResetInnerQueues(); }
    void ReportStat() override {}
    gfsim::SimQueue<gfsim_test::Number>* Queue() const { return queue_; }

private:
    gfsim::SimQueue<gfsim_test::Number>* queue_ = nullptr;
};

class Parent final : public gfsim::SimModule {
public:
    explicit Parent(gfsim::SimSystem* system) : SimModule("parent", system) {}

    void Build() override
    {
        SetObjectCapacity(3);
        root_ = AddInnerQueue(
            std::make_shared<gfsim::SimQueue<gfsim_test::Number>>("root"));
        inner_ = AddInnerQueue(std::make_shared<gfsim::SimQueue<Packet>>("inner"));
        inner_->SetTraceFormatter([](const Packet& packet) {
            return "id=" + std::to_string(packet.id) + " \"quoted\"\n";
        });
        child_ = std::make_shared<Child>();
        BindOutput(root_);
        BindInput(root_);
        BindOutput(inner_);
        BindInput(inner_);
        AddSubmodule(child_);
        BuildInnerQueues();
        BuildChildren();
    }

    void Work() override { WorkChildren(); }
    void Xfer() override
    {
        AdvanceInnerQueues();
        XferChildren();
    }
    void Reset() override
    {
        ResetInnerQueues();
        ResetChildren();
    }
    void ReportStat() override { ReportChildren(); }

    gfsim::SimQueue<Packet>* Inner() const { return inner_; }
    gfsim::SimQueue<gfsim_test::Number>* RootQueue() const { return root_; }
    gfsim::SimQueue<gfsim_test::Number>* ChildQueue() const
    {
        return child_->Queue();
    }

private:
    gfsim::SimQueue<gfsim_test::Number>* root_ = nullptr;
    gfsim::SimQueue<Packet>* inner_ = nullptr;
    std::shared_ptr<Child> child_;
};

void TestTrace(const std::string& path)
{
    std::ostringstream traceOutput;
    gfsim::SimSys system;
    auto parent = std::make_shared<Parent>(&system);
    system.AddModule(parent);
    system.Build();

    parent->RootQueue()->Write(gfsim_test::MakeNumber(5));
    parent->Inner()->Write(std::make_shared<Packet>(Packet{7}));
    parent->ChildQueue()->Write(gfsim_test::MakeNumber(9));
    system.EnableTrace(traceOutput);
    Check(system.GetTrace() != nullptr, "trace must be enabled");
    system.Step();
    system.GetTrace()->AddDuration("parent", "work", 0, 1, "sample \"x\"");
    system.GetTrace()->AddFlow("parent", "sink", 1, 2, "transfer");

    bool rejected = false;
    try {
        system.Reset();
    } catch (const std::logic_error&) {
        rejected = true;
    }
    Check(rejected, "active trace must be closed before Reset");
    system.DisableTrace();
    Check(system.GetTrace() == nullptr, "trace must be disabled");

    const std::string json = traceOutput.str();
    Check(json.find("{\"traceEvents\":[") == 0 && json.substr(json.size() - 2) == "]}",
          "trace must have a complete JSON root");
    Check(json.find("parent/inner") != std::string::npos &&
              json.find("parent/child/leaf") != std::string::npos &&
              json.find("parent/root") != std::string::npos,
          "root, parent, and nested child queues must be registered");
    Check(Count(json, "\"name\":\"queue_snapshot\"") == 6 &&
              Count(json, "\"name\":\"queue_depth\"") == 6,
          "three queues must be captured at cycles zero and one");
    Check(json.find("\"ts\":0") != std::string::npos &&
              json.find("\"ts\":1") != std::string::npos &&
              json.find("\"staged\":[\"5\"]") != std::string::npos &&
              json.find("\"visible\":[\"5\"]") != std::string::npos,
          "trace must capture cycle and both queue storage phases");
    Check(json.find("id=7 \\\"quoted\\\"\\n") != std::string::npos,
          "custom packet formatter must be JSON escaped");
    Check(json.find("\"ph\":\"X\"") != std::string::npos &&
              json.find("\"ph\":\"s\"") != std::string::npos &&
              json.find("\"ph\":\"f\"") != std::string::npos,
          "swimlane duration and dependency flow events must be present");

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << json;
    Check(static_cast<bool>(file), "trace fixture must be writable");
}

void TestFormatterRequirement()
{
    gfsim::SimQueue<Packet> queue("packet");
    queue.Write(std::make_shared<Packet>(Packet{1}));
    std::ostringstream output;
    gfsim::SimTrace trace(output);
    trace.RegisterQueue(queue, "packet");
    bool rejected = false;
    try {
        trace.Capture(0);
    } catch (const std::logic_error&) {
        rejected = true;
    }
    Check(rejected, "non-streamable payload must require an explicit formatter");
    trace.Close();
}

void TestSampling()
{
    std::ostringstream output;
    gfsim::SimSys system;
    auto parent = std::make_shared<Parent>(&system);
    system.AddModule(parent);
    system.Build();
    system.EnableTrace(output, 2);
    parent->RootQueue()->Write(gfsim_test::MakeNumber(3));
    system.Step();
    system.Step();
    system.DisableTrace();
    Check(Count(output.str(), "\"name\":\"queue_snapshot\"") == 6 &&
              output.str().find("\"ts\":2") != std::string::npos,
          "sampling interval must keep cycle zero and every second cycle");
}

} // namespace

int main(int argc, char** argv)
{
    try {
        Check(argc == 2, "trace test requires an output path");
        TestTrace(argv[1]);
        TestFormatterRequirement();
        TestSampling();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
