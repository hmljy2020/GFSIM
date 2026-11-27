#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

class Source final : public gfsim::SimModule {
public:
    explicit Source(gfsim::SimQueue<Packet>* output)
        : SimModule("source"), output_(BindOutput(output)) {}

    void Build() override {}
    void Work() override
    {
        if (next_ >= 2 || output_->Full()) {
            return;
        }
        auto packet = std::make_shared<Packet>(next_ + 1);
        produced_.push_back(packet); // retained only for pointer-identity checks
        output_->Write(std::move(packet));
        ++next_;
        GetSim()->MarkProgress();
        Log().Debug(Name(), "send") << "index=" << next_;
    }
    void Xfer() override {}
    void Reset() override { next_ = 0; produced_.clear(); }
    void ReportStat() override { Report().ReportVal("Source sent", next_); }

    const std::shared_ptr<Packet>& Produced(std::size_t index) const
    {
        return produced_.at(index);
    }

private:
    OUTPUT gfsim::SimQueue<Packet>* output_;
    int next_ = 0;
    std::vector<std::shared_ptr<Packet>> produced_;
};

class Transform final : public gfsim::SimModule {
public:
    Transform(gfsim::SimQueue<Packet>* input,
              gfsim::SimQueue<Packet>* output)
        : SimModule("transform"), input_(BindInput(input)),
          output_(BindOutput(output)) {}

    void Build() override {}
    void Work() override
    {
        if (input_->Empty() || output_->Full()) {
            return;
        }
        if (!Accept(input_->Peek())) {
            return;
        }
        auto result = std::make_shared<Packet>(Double(input_->Peek()));
        output_->Write(std::move(result));
        input_->Pop();
        ++fires_;
        GetSim()->MarkProgress();
        Log().Debug(Name(), "fire") << "count=" << fires_;
    }
    void Xfer() override {}
    void Reset() override { fires_ = 0; }
    void ReportStat() override { Report().ReportVal("Transform fires", fires_); }

    int Fires() const { return fires_; }

private:
    PURE static bool Accept(const Packet& input) { return input.value > 0; }
    PURE static int Double(const Packet& input) { return input.value * 2; }

    INPUT gfsim::SimQueue<Packet>* input_;
    OUTPUT gfsim::SimQueue<Packet>* output_;
    int fires_ = 0;
};

class Sink final : public gfsim::SimModule {
public:
    explicit Sink(gfsim::SimQueue<Packet>* input)
        : SimModule("sink"), input_(BindInput(input)) {}

    void Build() override {}
    void Work() override
    {
        if (blocked_ || input_->Empty()) {
            return;
        }
        values_.push_back(input_->Read()->value);
        GetSim()->MarkProgress();
        Log().Debug(Name(), "receive") << "count=" << values_.size();
    }
    void Xfer() override {}
    void Reset() override { blocked_ = true; values_.clear(); }
    void ReportStat() override { Report().ReportVal("Sink received", values_.size()); }

    void SetBlocked(bool blocked) { blocked_ = blocked; }
    const std::vector<int>& Values() const { return values_; }

private:
    INPUT gfsim::SimQueue<Packet>* input_;
    bool blocked_ = true;
    std::vector<int> values_;
};

class TransactionTop final : public gfsim::SimModule {
public:
    TransactionTop(gfsim::SimSystem* system, bool consumerFirst)
        : SimModule("transaction_top", system), consumerFirst_(consumerFirst) {}

    void Build() override
    {
        SetObjectCapacity(5); // two queues plus three children
        input_ = AddInnerQueue(std::make_shared<gfsim::SimQueue<Packet>>("source_to_transform"));
        output_ = AddInnerQueue(std::make_shared<gfsim::SimQueue<Packet>>("transform_to_sink"));
        input_->InitMaxSize(1);
        output_->InitMaxSize(1);
        source_ = std::make_shared<Source>(input_);
        transform_ = std::make_shared<Transform>(input_, output_);
        sink_ = std::make_shared<Sink>(output_);
        if (consumerFirst_) {
            AddSubmodule(sink_);
            AddSubmodule(transform_);
            AddSubmodule(source_);
        } else {
            AddSubmodule(source_);
            AddSubmodule(transform_);
            AddSubmodule(sink_);
        }
        BuildInnerQueues();
        BuildChildren();
    }
    void Work() override { WorkChildren(); }
    void Xfer() override { AdvanceInnerQueues(); XferChildren(); }
    void Reset() override { ResetInnerQueues(); ResetChildren(); }
    void ReportStat() override { ReportChildren(); }

    Source& Producer() const { return *source_; }
    Transform& Comb() const { return *transform_; }
    Sink& Consumer() const { return *sink_; }
    const gfsim::SimQueue<Packet>& InputQueue() const { return *input_; }
    const gfsim::SimQueue<Packet>& OutputQueue() const { return *output_; }

private:
    bool consumerFirst_;
    gfsim::SimQueue<Packet>* input_ = nullptr;
    gfsim::SimQueue<Packet>* output_ = nullptr;
    std::shared_ptr<Source> source_;
    std::shared_ptr<Transform> transform_;
    std::shared_ptr<Sink> sink_;
};

// Isolated admission test; a system-level version is exercised by TransactionTop.
class TwoInTwoOut final : public gfsim::SimModule {
public:
    TwoInTwoOut(gfsim::SimQueue<int>* first, gfsim::SimQueue<int>* second,
                gfsim::SimQueue<int>* sum, gfsim::SimQueue<int>* product)
        : SimModule("two_in_two_out"), first_(BindInput(first)),
          second_(BindInput(second)), sum_(BindOutput(sum)),
          product_(BindOutput(product)) {}
    void Build() override {}
    void Work() override
    {
        if (first_->Empty() || second_->Empty() || sum_->Full() || product_->Full()) {
            return;
        }
        const auto result = Compute(first_->Front(), second_->Front());
        sum_->Write(result.first);
        product_->Write(result.second);
        first_->Pop();
        second_->Pop();
        ++fires_;
    }
    void Xfer() override {}
    void Reset() override { fires_ = 0; }
    void ReportStat() override {}
    int Fires() const { return fires_; }

private:
    PURE static std::pair<int, int> Compute(int first, int second)
    {
        return std::make_pair(first + second, first * second);
    }

    INPUT gfsim::SimQueue<int>* first_;
    INPUT gfsim::SimQueue<int>* second_;
    OUTPUT gfsim::SimQueue<int>* sum_;
    OUTPUT gfsim::SimQueue<int>* product_;
    int fires_ = 0;
};

void TestAllPortsAdmitted()
{
    gfsim::SimQueue<int> first("first"), second("second");
    gfsim::SimQueue<int> sum("sum"), product("product");
    first.InitMaxSize(1);
    second.InitMaxSize(1);
    sum.InitMaxSize(1);
    product.InitMaxSize(1);
    TwoInTwoOut module(&first, &second, &sum, &product);

    first.Write(2);
    first.Work();
    module.Work();
    Check(module.Fires() == 0 && first.Size() == 1 && sum.WRBothEmpty(),
          "one missing input must prevent all output writes and input pops");

    second.Write(3);
    second.Work();
    product.Write(99);
    product.Work();
    module.Work();
    Check(module.Fires() == 0 && first.Size() == 1 && second.Size() == 1 &&
              sum.WRBothEmpty() && product.Front() == 99,
          "one full output must prevent partial multi-port commit");

    product.Pop();
    module.Work();
    Check(module.Fires() == 0 && !first.Empty() && !second.Empty() && sum.WRBothEmpty(),
          "a same-cycle pop must not grant a multi-port transaction");
    product.Work(); // publish the free slot
    module.Work();
    Check(module.Fires() == 1 && first.Size() == 1 && second.Size() == 1 &&
              sum.SizeW() == 1 && product.SizeW() == 1,
          "outputs stage while input consumption remains pending");
    first.Work();
    second.Work();
    Check(first.Empty() && second.Empty(), "input consumption commits at the boundary");
    sum.Work();
    product.Work();
    Check(sum.Read() == 5 && product.Read() == 6,
          "PURE computation must produce the expected two results");
}

void TestWriteCapacity()
{
    gfsim::SimQueue<int> queue("capacity");
    queue.InitMaxSize(3);
    queue.Write(1);
    queue.Work();
    Check(queue.Read() == 1 && queue.Front() == 1 && queue.Size() == 1 && !queue.Full(1) && queue.Full(2),
          "read must not enlarge this cycle's two-slot write budget");
    queue.Write(2);
    queue.Write(3);
    Check(queue.Full() && queue.toBeOverflow(std::numeric_limits<uint32_t>::max()),
          "staged writes must consume capacity and reservations must not wrap");
    bool rejected = false;
    try {
        queue.Write(4);
    } catch (const gfsim::AssertionError&) {
        rejected = true;
    }
    Check(rejected && queue.SizeW() == 2, "full write must fail without staging a packet");
    queue.setStall();
    queue.Work();
    Check(queue.Empty() && queue.SizeW() == 2 && !queue.Full() && queue.Full(1),
          "a stalled boundary commits consumption without publishing data");
    Check(queue.FlushIf([](int value) { return value == 2; }) == 1 && queue.Full(1),
          "quiescent flush releases its slot only at the next boundary");
    queue.Work();
    Check(queue.SizeW() == 1 && !queue.Full(1) && queue.Full(2),
          "a stalled boundary refreshes the budget after maintenance flush");
    queue.Reset();
    Check(queue.WRBothEmpty() && !queue.Full(2), "reset must restore all three slots");
}

void TestAtomicFire(bool consumerFirst)
{
    gfsim::SimSys system;
    auto top = std::make_shared<TransactionTop>(&system, consumerFirst);
    system.AddModule(top);
    system.Build();

    system.Step(); // source produces first packet
    Check(top->Comb().Fires() == 0 && top->OutputQueue().WRBothEmpty(),
          "empty input must not produce an output or fire PURE logic");
    system.Step(); // transform fires once, output becomes full
    system.Step(); // source produces second packet
    system.Step(); // transform must remain blocked on full output

    Check(top->Comb().Fires() == 1 && top->InputQueue().Size() == 1 &&
              top->OutputQueue().Full() &&
              top->InputQueue().Front() == top->Producer().Produced(1) &&
              top->InputQueue().Front()->value == 2,
          "full output must preserve the peeked input pointer and payload");

    top->Consumer().SetBlocked(false);
    system.Step(); // sink frees output; transform still sees it full this cycle
    Check(top->Comb().Fires() == 1 && top->InputQueue().Size() == 1,
          "a consumer release must not grant a transfer until the next cycle");
    system.Step(); // output Write succeeds, then input Pop commits
    Check(top->Comb().Fires() == 2 && top->InputQueue().Empty(),
          "successful output write must be followed by input consumption");
    system.Step(); // sink receives second result
    Check(top->Consumer().Values() == std::vector<int>({2, 4}) &&
              top->Producer().Produced(1)->value == 2,
          "PURE result must not mutate the input message before commit");
}

} // namespace

int main()
{
    try {
        TestWriteCapacity();
        TestAllPortsAdmitted();
        TestAtomicFire(false);
        TestAtomicFire(true);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
