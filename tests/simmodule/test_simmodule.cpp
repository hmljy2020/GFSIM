#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <type_traits>

#include "SimModule.h"
#include "../support/Payload.h"

namespace {

static_assert(std::is_abstract<gfsim::SimModule>::value,
              "SimModule must remain an abstract SimObj");

void Check(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class SumSplit final : public gfsim::SimModule {
public:
    SumSplit(gfsim::SimQueue<gfsim_test::Number>* a,
             gfsim::SimQueue<gfsim_test::Number>* b,
             gfsim::SimQueue<gfsim_test::Number>* c,
             gfsim::SimQueue<gfsim_test::Number>* sum,
             gfsim::SimQueue<gfsim_test::WideNumber>* twice)
        : SimModule("sum_split"),
          input1(BindInput(a)), input2(BindInput(b)), input3(BindInput(c)),
          output1(BindOutput(sum)), output2(BindOutput(twice))
    {
    }

    void Build() override {}

    void Work() override
    {
        if (input1->Empty() || input2->Empty() || input3->Empty() ||
            output1->Full() || output2->Full()) {
            return;
        }
        const int sum = input1->Peek().value + input2->Peek().value +
                        input3->Peek().value;
        const auto sumPacket = gfsim_test::MakeNumber(sum);
        const auto twicePacket = gfsim_test::MakeWideNumber(2L * sum);
        output1->Write(sumPacket);
        output2->Write(twicePacket);
        input1->Pop();
        input2->Pop();
        input3->Pop();
        ++accepted_;
        Log().Info("sum_split", "accept") << "sum=" << sum;
    }

    void Xfer() override {}
    void Reset() override { accepted_ = 0; }
    void ReportStat() override { Report().ReportVal("Accepted sums", accepted_); }

private:
    INPUT gfsim::SimQueue<gfsim_test::Number>* input1;
    INPUT gfsim::SimQueue<gfsim_test::Number>* input2;
    INPUT gfsim::SimQueue<gfsim_test::Number>* input3;
    OUTPUT gfsim::SimQueue<gfsim_test::Number>* output1;
    OUTPUT gfsim::SimQueue<gfsim_test::WideNumber>* output2;
    int accepted_ = 0;
};

class Merge final : public gfsim::SimModule {
public:
    Merge(gfsim::SimQueue<gfsim_test::Number>* first,
          gfsim::SimQueue<gfsim_test::WideNumber>* second,
          gfsim::SimQueue<gfsim_test::WideNumber>* result)
        : SimModule("merge"), first_(BindInput(first)), second_(BindInput(second)),
          result_(BindOutput(result))
    {
    }

    void Build() override {}

    void Work() override
    {
        if (first_->Empty() || second_->Empty() || result_->Full()) {
            return;
        }
        const long value = first_->Peek().value + second_->Peek().value;
        result_->Write(gfsim_test::MakeWideNumber(value));
        first_->Pop();
        second_->Pop();
        ++merged_;
        Log().Info("merge", "emit") << "count=" << merged_;
    }

    void Xfer() override {}
    void Reset() override { merged_ = 0; }
    void ReportStat() override { Report().ReportVal("Merged results", merged_); }

private:
    INPUT gfsim::SimQueue<gfsim_test::Number>* first_;
    INPUT gfsim::SimQueue<gfsim_test::WideNumber>* second_;
    OUTPUT gfsim::SimQueue<gfsim_test::WideNumber>* result_;
    int merged_ = 0;
};

class Relay final : public gfsim::SimModule {
public:
    Relay(std::string name, gfsim::SimQueue<int>* input,
          gfsim::SimQueue<int>* output)
        : SimModule(std::move(name)), input_(BindInput(input)),
          output_(BindOutput(output)) {}
    void Build() override {}
    void Work() override
    {
        if (!input_->Empty() && !output_->Full()) {
            output_->Write(input_->Front());
            input_->Pop();
        }
    }
    void Xfer() override {}
    void Reset() override {}
    void ReportStat() override {}

private:
    gfsim::SimQueue<int>* input_;
    gfsim::SimQueue<int>* output_;
};

class CyclicTopology final : public gfsim::SimModule {
public:
    explicit CyclicTopology(gfsim::SimSystem* system)
        : SimModule("cycle", system) {}
    void Build() override
    {
        SetObjectCapacity(4);
        auto* forward = AddInnerQueue(
            std::make_shared<gfsim::SimQueue<int>>("forward"));
        auto* backward = AddInnerQueue(
            std::make_shared<gfsim::SimQueue<int>>("backward"));
        AddSubmodule(std::make_shared<Relay>("left", backward, forward));
        AddSubmodule(std::make_shared<Relay>("right", forward, backward));
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
};

class Topology final : public gfsim::SimModule {
public:
    explicit Topology(gfsim::SimSystem* system) : SimModule("topology", system) {}

    void Build() override
    {
        SetObjectCapacity(8);
        a_ = AddInnerQueue(
            std::make_shared<gfsim::SimQueue<gfsim_test::Number>>("a"));
        b_ = AddInnerQueue(
            std::make_shared<gfsim::SimQueue<gfsim_test::Number>>("b"));
        c_ = AddInnerQueue(
            std::make_shared<gfsim::SimQueue<gfsim_test::Number>>("c"));
        auto* sum = AddInnerQueue(
            std::make_shared<gfsim::SimQueue<gfsim_test::Number>>("sum"));
        auto* twice = AddInnerQueue(
            std::make_shared<gfsim::SimQueue<gfsim_test::WideNumber>>("twice"));
        result_ = AddInnerQueue(
            std::make_shared<gfsim::SimQueue<gfsim_test::WideNumber>>("result"));
        BindOutput(a_);
        BindOutput(b_);
        BindOutput(c_);
        BindInput(result_);
        AddSubmodule(std::make_shared<SumSplit>(a_, b_, c_, sum, twice));
        AddSubmodule(std::make_shared<Merge>(sum, twice, result_));
        BuildInnerQueues();
        BuildChildren();
    }

    int workCount = 0;
    int xferCount = 0;
    int resetCount = 0;
    int reportCount = 0;

    void Work() override
    {
        ++workCount;
        WorkChildren();
    }

    void Xfer() override
    {
        ++xferCount;
        AdvanceInnerQueues();
        XferChildren();
    }

    void Reset() override
    {
        ++resetCount;
        ResetInnerQueues();
        ResetChildren();
    }

    void ReportStat() override
    {
        ++reportCount;
        Report().ReportVal("Topology cycles", workCount);
        ReportChildren();
    }

    void Submit(int a, int b, int c)
    {
        a_->Write(gfsim_test::MakeNumber(a));
        b_->Write(gfsim_test::MakeNumber(b));
        c_->Write(gfsim_test::MakeNumber(c));
    }

    std::shared_ptr<gfsim_test::WideNumber> ReadResult()
    {
        return result_->Read();
    }

    bool ResultEmpty() { return result_->Empty(); }

private:
    INPUT gfsim::SimQueue<gfsim_test::Number>* a_;
    INPUT gfsim::SimQueue<gfsim_test::Number>* b_;
    INPUT gfsim::SimQueue<gfsim_test::Number>* c_;
    OUTPUT gfsim::SimQueue<gfsim_test::WideNumber>* result_;
};

void TestHierarchyAndPorts()
{
    gfsim::SimSys system;
    std::ostringstream report;
    system.GetReporter().SetOutput(report);
    auto root = std::make_shared<Topology>(&system);
    system.AddModule(root);
    system.Build();
    Check(root->Parent() == nullptr && root->GetSim() == &system,
          "root module must expose its owning system");
    Check(root->Submodules().size() == 2, "root must own both child modules");
    for (const auto& child : root->Submodules()) {
        Check(child->Parent() == root.get() && child->GetSim() == &system,
              "child must find its parent and system");
    }

    root->Submit(1, 2, 3);
    system.Step();
    Check(root->ResultEmpty(), "source queues publish only after Work");
    system.Step();
    Check(root->ResultEmpty(), "sum queues publish before merge can consume");
    system.Step();
    Check(root->ReadResult()->value == 18,
          "three inputs must flow through two child modules");
    Check(root->workCount == 3 && root->xferCount == 3,
          "root methods must run once per system step");

    system.ReportStat();
    Check(root->reportCount == 1 &&
              report.str().find("Accepted sums") != std::string::npos &&
              report.str().find("Merged results") != std::string::npos,
          "report must traverse parent and child modules");
    root->Submit(9, 8, 7);
    system.Reset();
    Check(root->resetCount == 2 && root->ResultEmpty(),
          "reset must clear root state and external queues");
}

void TestInvalidWiring()
{
    gfsim::SimQueue<gfsim_test::Number> a("a"), b("b"), c("c"), out1("out1");
    gfsim::SimQueue<gfsim_test::WideNumber> out2("out2"), result("result");
    bool rejected = false;
    try {
        SumSplit bad(&a, &a, &c, &out1, &out2);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Check(rejected, "duplicate input queue must be rejected");

    rejected = false;
    try {
        SumSplit bad(&a, &b, &c, &out1, nullptr);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Check(rejected, "null output queue must be rejected");

    gfsim::SimSys system;
    auto first = std::make_shared<Topology>(&system);
    system.AddModule(first);
    system.Build();
    auto child = first->Submodules().front();
    rejected = false;
    try {
        first->AddSubmodule(child);
    } catch (const std::logic_error&) {
        rejected = true;
    }
    Check(rejected, "module object collection must freeze after Build");

    gfsim::SimSys cyclicSystem;
    cyclicSystem.AddModule(std::make_shared<CyclicTopology>(&cyclicSystem));
    cyclicSystem.Build();
    Check(cyclicSystem.IsBuilt(),
          "fully connected queue cycles must be accepted during Build");
}

} // namespace

int main()
{
    try {
        TestHierarchyAndPorts();
        TestInvalidWiring();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
