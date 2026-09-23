#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "SimModule.h"
#include "../support/Payload.h"

namespace {

void Check(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class PipelineStage final : public gfsim::SimModule {
public:
    PipelineStage(std::string name, gfsim::SimQueue<gfsim_test::Number>* input,
                  gfsim::SimQueue<gfsim_test::Number>* output, int multiplier,
                  int offset, bool finalStage = false)
        : SimModule(std::move(name)), input_(BindInput(input)),
          output_(BindOutput(output)), multiplier_(multiplier),
          offset_(offset), finalStage_(finalStage)
    {
    }

    void Build() override { ++buildCount_; }

    void Work() override
    {
        if (input_->Empty() || output_->Full()) {
            return;
        }
        const int result = input_->Peek().value * multiplier_ + offset_;
        const auto packet = input_->Front();
        output_->Write(packet);
        packet->value = result;
        input_->Pop();
        ++processed_;
        GetSim()->MarkProgress();
        Log().Info(Name(), "work") << "result=" << result;
        if (finalStage_) {
            GetSim()->RequestTerminate("pipeline complete");
        }
    }

    void Xfer() override {}
    void Reset() override { processed_ = 0; }
    void ReportStat() override
    {
        Report().ReportVal(Name() + " processed", processed_);
    }

    int BuildCount() const { return buildCount_; }

private:
    INPUT gfsim::SimQueue<gfsim_test::Number>* input_;
    OUTPUT gfsim::SimQueue<gfsim_test::Number>* output_;
    int multiplier_;
    int offset_;
    bool finalStage_;
    int buildCount_ = 0;
    int processed_ = 0;
};

class Pipeline final : public gfsim::SimModule {
public:
    explicit Pipeline(gfsim::SimSystem* system) : SimModule("pipeline", system) {}

    void Build() override
    {
        SetObjectCapacity(7);
        input_ = AddInnerQueue(
            std::make_shared<gfsim::SimQueue<gfsim_test::Number>>("ingress"));
        auto* firstToSecond = AddInnerQueue(
            std::make_shared<gfsim::SimQueue<gfsim_test::Number>>(
                "stage1_to_stage2"));
        auto* secondToThird = AddInnerQueue(
            std::make_shared<gfsim::SimQueue<gfsim_test::Number>>(
                "stage2_to_stage3"));
        output_ = AddInnerQueue(
            std::make_shared<gfsim::SimQueue<gfsim_test::Number>>("egress"));
        BindOutput(input_);
        BindInput(output_);
        first_ = std::make_shared<PipelineStage>("stage1", input_, firstToSecond, 1, 1);
        second_ = std::make_shared<PipelineStage>("stage2", firstToSecond,
                                                  secondToThird, 2, 0);
        third_ = std::make_shared<PipelineStage>("stage3", secondToThird,
                                                 output_, 1, -3);
        AddSubmodule(first_);
        AddSubmodule(second_);
        AddSubmodule(third_);
        BuildInnerQueues();
        BuildChildren();
    }

    void Work() override { WorkChildren(); }
    void Xfer() override
    {
        AdvanceInnerQueues();
        XferChildren();
        if (!output_->Empty()) {
            completed_ = output_->Read();
            GetSim()->MarkProgress();
            GetSim()->RequestTerminate("pipeline complete");
        }
    }
    void Reset() override
    {
        ResetInnerQueues();
        ResetChildren();
        completed_.reset();
    }
    void ReportStat() override
    {
        Report().ReportTitle("Pipeline Stats");
        ReportChildren();
    }

    int ChildBuildCount() const
    {
        return first_->BuildCount() + second_->BuildCount() + third_->BuildCount();
    }

    void Submit(std::shared_ptr<gfsim_test::Number> packet)
    {
        input_->Write(std::move(packet));
    }

    const std::shared_ptr<gfsim_test::Number>& Completed() const
    {
        return completed_;
    }

private:
    INPUT gfsim::SimQueue<gfsim_test::Number>* input_;
    OUTPUT gfsim::SimQueue<gfsim_test::Number>* output_;
    std::shared_ptr<PipelineStage> first_;
    std::shared_ptr<PipelineStage> second_;
    std::shared_ptr<PipelineStage> third_;
    std::shared_ptr<gfsim_test::Number> completed_;
};

void TestThreeStagePipeline()
{
    gfsim::SimSys system;
    auto pipeline = std::make_shared<Pipeline>(&system);
    std::ostringstream report;
    std::ostringstream log;
    system.GetReporter().SetOutput(report);
    system.GetLogger().SetOutput(log);
    system.GetLogger().SetLevel(gfsim::LogLevel::Info);
    system.AddModule(pipeline);

    bool rejected = false;
    try {
        system.Step();
    } catch (const std::logic_error&) {
        rejected = true;
    }
    Check(rejected, "Step must require Build");

    system.SetDeadlockThreshold(2);
    system.Build();
    Check(system.IsBuilt() && pipeline->Name() == "pipeline" &&
              pipeline->Submodules().size() == 3 &&
              pipeline->ChildBuildCount() == 3,
          "Build must construct and build exactly three named children");
    for (const auto& child : pipeline->Submodules()) {
        Check(child->Parent() == pipeline.get() && child->GetSim() == &system,
              "each child must have its parent and system context");
    }

    const auto firstPacket = gfsim_test::MakeNumber(10);
    pipeline->Submit(firstPacket);
    system.Step();
    Check(!system.NeedTerminate(),
          "ingress must publish before the first stage can consume");
    system.Step();
    Check(!system.NeedTerminate(),
          "stage1 must not skip downstream pipeline stages");
    system.Step();
    Check(!system.NeedTerminate(),
          "stage2 must hand off to stage3 on the next cycle");
    system.Step();
    Check(system.Cycles() == 4 && system.NeedTerminate() &&
              system.TerminationReason() == "pipeline complete" &&
              pipeline->Completed() == firstPacket &&
              pipeline->Completed()->value == 19,
          "root sink must receive the original packet after three stages");
    Check(log.str().find("[C:4][stage3.work][INFO] result=19") !=
              std::string::npos,
          "stage logs must carry names and cycle numbers");
    system.ReportStat();
    Check(report.str().find("stage1 processed") != std::string::npos &&
              report.str().find("stage2 processed") != std::string::npos &&
              report.str().find("stage3 processed") != std::string::npos,
          "parent report must include all three children");

    rejected = false;
    try {
        system.Step();
    } catch (const std::logic_error&) {
        rejected = true;
    }
    Check(rejected, "Step must reject stepping after termination");

    system.Reset();
    const auto secondPacket = gfsim_test::MakeNumber(1);
    pipeline->Submit(secondPacket);
    system.Run(5);
    Check(system.Cycles() == 4 && pipeline->Completed() == secondPacket &&
              pipeline->Completed()->value == 1,
          "Run must preserve packet identity and reset must clear prior state");

    system.Reset();
    system.SetDeadlockThreshold(3);
    bool deadlocked = false;
    try {
        system.Run(10);
    } catch (const gfsim::DeadlockError&) {
        deadlocked = true;
    }
    Check(deadlocked && system.IsDeadlocked() && system.Cycles() == 3 &&
              system.StalledCycles() == 3 && !system.NeedTerminate(),
          "no-progress threshold must raise a deadlock error");

    bool refused = false;
    try {
        system.Step();
    } catch (const gfsim::DeadlockError&) {
        refused = true;
    }
    Check(refused && system.Cycles() == 3,
          "deadlocked system must reject later steps");

    system.Reset();
    Check(!system.IsDeadlocked() && system.StalledCycles() == 0,
          "Reset must clear deadlock state");
}

} // namespace

int main()
{
    try {
        TestThreeStagePipeline();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
