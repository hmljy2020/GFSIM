#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "SimTestbench.h"
#include "../support/Payload.h"

namespace {

void Check(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class Doubler final : public gfsim::SimModule {
public:
    Doubler(gfsim::SimSystem* system,
            gfsim::SimQueue<gfsim_test::Number>* input,
            gfsim::SimQueue<gfsim_test::Number>* output)
        : SimModule("doubler", system), input_(BindInput(input)),
          output_(BindOutput(output)) {}

    void Build() override {}
    void Work() override
    {
        if (input_->Empty() || output_->Full()) {
            return;
        }
        const int value = input_->Peek().value * 2;
        const auto packet = input_->Front();
        output_->Write(packet);
        packet->value = value;
        input_->Pop();
        ++processed_;
        GetSim()->MarkProgress();
        Log().Debug(Name(), "work") << "value=" << value;
    }
    void Xfer() override {}
    void Reset() override { processed_ = 0; }
    void ReportStat() override { Report().ReportVal("Doubled values", processed_); }

private:
    INPUT gfsim::SimQueue<gfsim_test::Number>* input_;
    OUTPUT gfsim::SimQueue<gfsim_test::Number>* output_;
    std::uint64_t processed_ = 0;
};

class QueueBench final : public gfsim::SimTestbench {
public:
    QueueBench(gfsim::SimSystem* system,
               gfsim::SimQueue<gfsim_test::Number>* stimulus,
               gfsim::SimQueue<gfsim_test::Number>* observed,
               std::vector<int> expected)
        : SimTestbench("queue_bench", system),
          stimulus_(BindOutput(stimulus)), observed_(BindInput(observed)),
          expected_(std::move(expected)) {}

    void Build() override {}

    bool HasObservedOutput() const { return !observed_->Empty(); }
    int ObservedValue() const { return observed_->Peek().value; }

protected:
    void DriveStimuli() override
    {
        if (sent_ == stimuli_.size() || stimulus_->Full()) {
            return;
        }
        auto packet = gfsim_test::MakeNumber(stimuli_[sent_]);
        // Retain one handle to verify pointer identity after the DUT returns it.
        produced_.push_back(packet);
        stimulus_->Write(packet);
        RecordStimulus("item " + std::to_string(sent_));
        ++sent_;
    }

    void CheckOutputs() override
    {
        if (observed_->Empty()) {
            return;
        }
        GFSIM_ASSERT(checked_ < expected_.size()) << "unexpected extra output";
        const auto& actual = observed_->Front();
        GFSIM_ASSERT(actual == produced_[checked_])
            << "pipeline replaced packet " << checked_;
        ExpectEqual(actual->value, expected_[checked_],
                    "item " + std::to_string(checked_));
        observed_->Pop();
        ++checked_;
    }

    bool Done() const override
    {
        return sent_ == stimuli_.size() && checked_ == expected_.size();
    }

    void ResetBench() override
    {
        sent_ = checked_ = 0;
        produced_.clear();
    }

    void ReportBench() override
    {
        Report().ReportVal("Expected values", expected_.size());
    }

private:
    OUTPUT gfsim::SimQueue<gfsim_test::Number>* stimulus_;
    INPUT gfsim::SimQueue<gfsim_test::Number>* observed_;
    const std::vector<int> stimuli_{1, 2, 3};
    std::vector<int> expected_;
    std::vector<std::shared_ptr<gfsim_test::Number>> produced_;
    std::size_t sent_ = 0;
    std::size_t checked_ = 0;
};

class BenchHarness final : public gfsim::SimModule {
public:
    BenchHarness(gfsim::SimSystem* system, std::vector<int> expected)
        : SimModule("bench_harness", system), expected_(std::move(expected)) {}

    void Build() override
    {
        SetObjectCapacity(4);
        auto* stimulus = AddInnerQueue(
            std::make_shared<gfsim::SimQueue<gfsim_test::Number>>("stimulus"));
        auto* observed = AddInnerQueue(
            std::make_shared<gfsim::SimQueue<gfsim_test::Number>>("observed"));
        dut_ = std::make_shared<Doubler>(GetSim(), stimulus, observed);
        bench_ = std::make_shared<QueueBench>(GetSim(), stimulus, observed,
                                               expected_);
        AddSubmodule(dut_);
        AddSubmodule(bench_);
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

    const QueueBench& Bench() const { return *bench_; }

private:
    std::vector<int> expected_;
    std::shared_ptr<Doubler> dut_;
    std::shared_ptr<QueueBench> bench_;
};

void TestBench(const std::vector<int>& expected, bool mismatch)
{
    gfsim::SimSys system;
    auto harness = std::make_shared<BenchHarness>(&system, expected);
    std::ostringstream report;
    std::ostringstream log;
    system.GetReporter().SetOutput(report);
    system.GetLogger().SetOutput(log);
    system.SetDeadlockThreshold(4);

    system.AddModule(harness);
    system.Build();

    if (mismatch) {
        bool failed = false;
        try {
            system.Run(10);
        } catch (const gfsim::AssertionError& error) {
            failed = std::string(error.what()).find("queue_bench item 1") !=
                     std::string::npos;
        }
        Check(failed && !system.NeedTerminate() &&
                  harness->Bench().HasObservedOutput() &&
                  harness->Bench().ObservedValue() == 4,
              "mismatched output must raise context without consuming output");
        return;
    }

    system.Run(10);
    Check(system.Cycles() == 5 && system.NeedTerminate() &&
              system.TerminationReason() == "queue_bench complete" &&
              harness->Bench().StimuliSent() == 3 &&
              harness->Bench().OutputsChecked() == 3,
          "testbench must drive and verify all three values before termination");
    system.ReportStat();
    Check(report.str().find("Stimuli sent") != std::string::npos &&
              report.str().find("Outputs checked") != std::string::npos &&
              report.str().find("Doubled values") != std::string::npos,
          "testbench and DUT counters must be reportable");

    system.Reset();
    Check(harness->Bench().StimuliSent() == 0 &&
              harness->Bench().OutputsChecked() == 0,
          "Reset must clear testbench counters");
    system.Run(10);
    Check(harness->Bench().OutputsChecked() == 3,
          "testbench must be reusable after Reset");
}

void TestAssert()
{
    int evaluations = 0;
    GFSIM_ASSERT(++evaluations == 1) << "ignored detail";
    Check(evaluations == 1, "assertion condition must be evaluated once");

    bool failed = false;
    try {
        GFSIM_ASSERT(evaluations == 2) << "context=0x" << std::hex << 15;
    } catch (const gfsim::AssertionError& error) {
        const std::string message = error.what();
        failed = message.find("evaluations == 2") != std::string::npos &&
                 message.find("context=0xf") != std::string::npos &&
                 message.find("test_simtestbench.cpp") != std::string::npos;
    }
    Check(failed, "assertion must preserve expression, context, and source location");

    gfsim::SimQueue<int> empty("empty");
    Check(empty.MaxSize() == 512,
          "queues must use the platform-wide default capacity");
    failed = false;
    try {
        empty.Read();
    } catch (const gfsim::AssertionError&) {
        failed = true;
    }
    Check(failed, "queue preconditions must use the assertion interface");

    gfsim::SimQueue<int> full("full");
    full.InitMaxSize(1);
    full.Write(1);
    failed = false;
    try {
        full.Write(2);
    } catch (const gfsim::AssertionError&) {
        failed = true;
    }
    Check(failed && full.SizeW() == 1 && full.GetRawWriteData().front() == 1,
          "full-queue assertion must leave the original value staged");

    gfsim::SimQueue<bool> flag("flag");
    flag.Write(true);
    flag.Work();
    Check(flag.Read(), "fundamental bool payloads must pass directly by value");
}

} // namespace

int main()
{
    try {
        TestAssert();
        TestBench(std::vector<int>{2, 4, 6}, false);
        TestBench(std::vector<int>{2, 5, 6}, true);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
