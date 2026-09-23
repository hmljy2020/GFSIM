#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
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

class Stage final : public gfsim::SimModule {
public:
    Stage(std::string name, gfsim::SimQueue<Packet>* input,
          gfsim::SimQueue<Packet>* output)
        : SimModule(std::move(name)), input_(BindInput(input)),
          output_(BindOutput(output)) {}

    void Build() override {}
    void Work() override
    {
        if (input_->Empty() || output_->Full()) {
            return;
        }
        output_->Write(input_->Front());
        input_->Pop();
        GetSim()->MarkProgress();
    }
    void Xfer() override {}
    void Reset() override {}
    void ReportStat() override {}

private:
    INPUT gfsim::SimQueue<Packet>* input_;
    OUTPUT gfsim::SimQueue<Packet>* output_;
};

class Pipeline final : public gfsim::SimModule {
public:
    explicit Pipeline(gfsim::SimSystem* system)
        : SimModule("Pipeline", system) {}

    void Build() override
    {
        SetObjectCapacity(5); // three physical queues and two child modules
        auto* input = AddInnerQueue(
            std::make_shared<gfsim::SimQueue<Packet>>("input queue"));
        auto* middle = AddInnerQueue(
            std::make_shared<gfsim::SimQueue<Packet>>("middle queue"));
        auto* output = AddInnerQueue(
            std::make_shared<gfsim::SimQueue<Packet>>("output queue"));
        input_ = BindOutput(input);
        output_ = BindInput(output);
        AddSubmodule(std::make_shared<Stage>("Stage A", input, middle));
        AddSubmodule(std::make_shared<Stage>("Stage B", middle, output));
        BuildInnerQueues();
        BuildChildren();
    }
    void Work() override
    {
        if (!sent_ && !input_->Full()) {
            input_->Write(std::make_shared<Packet>(1));
            sent_ = true;
            GetSim()->MarkProgress();
        }
        if (!output_->Empty()) {
            output_->Read();
            GetSim()->MarkProgress();
        }
        WorkChildren();
    }
    void Xfer() override { AdvanceInnerQueues(); XferChildren(); }
    void Reset() override { sent_ = false; ResetInnerQueues(); ResetChildren(); }
    void ReportStat() override { ReportChildren(); }

private:
    OUTPUT gfsim::SimQueue<Packet>* input_ = nullptr;
    INPUT gfsim::SimQueue<Packet>* output_ = nullptr;
    bool sent_ = false;
};

void TestGraph(const std::string& path)
{
    gfsim::SimSys system;
    auto pipeline = std::make_shared<Pipeline>(&system);
    system.AddModule(pipeline);

    bool rejected = false;
    try {
        std::ostringstream beforeBuild;
        system.WriteDot(beforeBuild);
    } catch (const std::logic_error&) {
        rejected = true;
    }
    Check(rejected, "DOT generation must require a built system");

    system.Build();
    std::ostringstream graph;
    system.WriteDot(graph);
    const std::string dot = graph.str();
    Check(dot.find("digraph GFSIM") != std::string::npos &&
              dot.find("subgraph cluster_m0") != std::string::npos &&
              dot.find("subgraph cluster_m1") != std::string::npos &&
              dot.find("subgraph cluster_m2") != std::string::npos,
          "parent and two children must form nested module subgraphs");
    Check(dot.find("shape=cylinder") != std::string::npos &&
              dot.find("middle queue") != std::string::npos &&
              dot.find("input queue") != std::string::npos &&
              dot.find("output queue") != std::string::npos,
          "named queues must be visible as queue-shaped nodes");
    Check(dot.find("m0 -> q0") != std::string::npos &&
              dot.find("q0 -> m1") != std::string::npos &&
              dot.find("m1 -> q1") != std::string::npos &&
              dot.find("q1 -> m2") != std::string::npos &&
              dot.find("m2 -> q2") != std::string::npos &&
              dot.find("q2 -> m0") != std::string::npos,
          "input and output bindings must become directed queue edges");

    system.SaveDot(path);
    std::ifstream saved(path);
    std::ostringstream savedText;
    savedText << saved.rdbuf();
    Check(savedText.str() == dot, "SaveDot must write the same graph as WriteDot");
}

} // namespace

int main(int argc, char** argv)
{
    try {
        Check(argc == 2, "graph test requires an output path");
        TestGraph(argv[1]);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
