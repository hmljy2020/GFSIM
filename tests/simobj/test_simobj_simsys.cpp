#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "SimModule.h"
#include "../support/Payload.h"

namespace {

void Check(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class Probe final : public gfsim::SimModule {
public:
    Probe(std::vector<std::string>& events, const std::string& name, bool& destroyed)
        : SimModule(name), events_(events), name_(name), destroyed_(destroyed)
    {
    }

    ~Probe() override { destroyed_ = true; }

    void Build() override
    {
        events_.push_back("Build:" + name_);
        ++buildCount;
    }

    void Work() override
    {
        events_.push_back("Work:" + name_);
        ++workCount;
        ++value;
    }

    void Xfer() override
    {
        events_.push_back("Xfer:" + name_);
        ++xferCount;
    }

    void Reset() override
    {
        events_.push_back("Reset:" + name_);
        ++resetCount;
        value = 0;
    }

    void ReportStat() override
    {
        events_.push_back("ReportStat:" + name_);
        ++reportCount;
    }

    int buildCount = 0;
    int workCount = 0;
    int xferCount = 0;
    int resetCount = 0;
    int reportCount = 0;
    int value = 0;

private:
    std::vector<std::string>& events_;
    std::string name_;
    bool& destroyed_;
};

class ProbeRoot final : public gfsim::SimModule {
public:
    ProbeRoot(gfsim::SimSystem* system, std::shared_ptr<Probe> first,
              std::shared_ptr<Probe> second)
        : SimModule("probe_root", system), first_(std::move(first)),
          second_(std::move(second)) {}

    void Build() override
    {
        SetObjectCapacity(2);
        AddSubmodule(first_);
        AddSubmodule(second_);
        BuildChildren();
    }
    void Work() override { WorkChildren(); }
    void Xfer() override { XferChildren(); }
    void Reset() override { ResetChildren(); }
    void ReportStat() override { ReportChildren(); }

private:
    std::shared_ptr<Probe> first_;
    std::shared_ptr<Probe> second_;
};

void TestSimObjAndSimSys()
{
    static_assert(std::is_same<gfsim::SimSys, gfsim::SimSystem>::value,
                  "SimSys must be an alias of SimSystem");

    std::vector<std::string> events;
    bool firstDestroyed = false;
    bool secondDestroyed = false;
    std::weak_ptr<Probe> firstWeak;

    {
        gfsim::SimSys system;
        auto first = std::make_shared<Probe>(events, "first", firstDestroyed);
        auto second = std::make_shared<Probe>(events, "second", secondDestroyed);
        firstWeak = first;

        Check(first->Name() == "first" && second->Name() == "second",
              "SimObj names must be stored for bookkeeping");
        Check(first->GetSim() == nullptr,
              "unattached SimModule must not invent a system owner");
        Check(system.Cycles() == 0, "new system must start at cycle zero");
        auto root = std::make_shared<ProbeRoot>(&system, first, second);
        system.AddModule(root);
        first.reset();
        Check(!firstWeak.expired(), "AddModule must retain its module");

        system.Build();
        Check(second->buildCount == 1 && system.IsBuilt(),
              "Build must visit each registered object once");
        events.clear();

        bool rejected = false;
        try {
            system.Build();
        } catch (const std::logic_error&) {
            rejected = true;
        }
        Check(rejected, "Build must run only once per system");
        rejected = false;
        try {
            system.AddModule(std::make_shared<ProbeRoot>(&system, first, second));
        } catch (const std::logic_error&) {
            rejected = true;
        }
        Check(rejected, "root registration must close when Build starts");
        rejected = false;
        try {
            system.MarkProgress();
        } catch (const std::logic_error&) {
            rejected = true;
        }
        Check(rejected, "progress must be reported during a step");

        system.Step();
        Check(system.Cycles() == 1, "Step must increment the cycle count");
        Check(events == std::vector<std::string>({"Work:first", "Work:second",
                                                  "Xfer:first", "Xfer:second"}),
              "Step must finish all Work calls before any Xfer call");
        Check(second->workCount == 1 && second->xferCount == 1 && second->value == 1,
              "Step must invoke each SimObj phase once");

        events.clear();
        system.ReportStat();
        Check(events == std::vector<std::string>({"ReportStat:first", "ReportStat:second"}),
              "ReportStat must visit modules in registration order");
        Check(second->reportCount == 1, "ReportStat must reach each module");

        events.clear();
        system.Reset();
        Check(system.Cycles() == 0, "Reset must clear the cycle count");
        Check(events == std::vector<std::string>({"Reset:first", "Reset:second"}),
              "Reset must visit modules in registration order");
        Check(second->resetCount == 2 && second->value == 0,
              "Reset must reach each module");

        events.clear();
        system.Step();
        Check(system.Cycles() == 1 && second->workCount == 2 && second->value == 1,
              "Reset must keep modules registered for later steps");
    }

    Check(firstWeak.expired() && firstDestroyed && secondDestroyed,
          "SimObj virtual destructor must run after system ownership ends");

    bool rejected = false;
    try {
        Probe unnamed(events, "", firstDestroyed);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Check(rejected, "SimObj must reject an empty name");
}

class ValueSource final : public gfsim::SimModule {
public:
    ValueSource(gfsim::SimQueue<int>* number, gfsim::SimQueue<bool>* flag)
        : SimModule("value_source"), number_(BindOutput(number)),
          flag_(BindOutput(flag)) {}
    void Build() override {}
    void Work() override
    {
        if (!sent_) {
            number_->Write(17);
            flag_->Write(true);
            sent_ = true;
            GetSim()->MarkProgress();
        }
    }
    void Xfer() override {}
    void Reset() override { sent_ = false; }
    void ReportStat() override {}

private:
    gfsim::SimQueue<int>* number_;
    gfsim::SimQueue<bool>* flag_;
    bool sent_ = false;
};

class ValueSink final : public gfsim::SimModule {
public:
    ValueSink(gfsim::SimQueue<int>* number, gfsim::SimQueue<bool>* flag)
        : SimModule("value_sink"), number_(BindInput(number)),
          flag_(BindInput(flag)) {}
    void Build() override {}
    void Work() override
    {
        if (!number_->Empty() && !flag_->Empty()) {
            numberValue = number_->Read();
            flagValue = flag_->Read();
            GetSim()->MarkProgress();
        }
    }
    void Xfer() override {}
    void Reset() override
    {
        numberValue = 0;
        flagValue = false;
    }
    void ReportStat() override {}

    int numberValue = 0;
    bool flagValue = false;

private:
    gfsim::SimQueue<int>* number_;
    gfsim::SimQueue<bool>* flag_;
};

class ValueRoot final : public gfsim::SimModule {
public:
    explicit ValueRoot(gfsim::SimSystem* system) : SimModule("value_root", system) {}
    void Build() override
    {
        SetObjectCapacity(4);
        auto* number = AddInnerQueue(std::make_shared<gfsim::SimQueue<int>>("number"));
        auto* flag = AddInnerQueue(std::make_shared<gfsim::SimQueue<bool>>("flag"));
        source_ = std::make_shared<ValueSource>(number, flag);
        sink_ = std::make_shared<ValueSink>(number, flag);
        AddSubmodule(source_);
        AddSubmodule(sink_);
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
    const ValueSink& Sink() const { return *sink_; }

private:
    std::shared_ptr<ValueSource> source_;
    std::shared_ptr<ValueSink> sink_;
};

void TestDirectValueQueues()
{
    gfsim::SimSys system;
    auto root = std::make_shared<ValueRoot>(&system);
    system.AddModule(root);
    system.Build();
    system.Step();
    Check(root->Sink().numberValue == 0 && !root->Sink().flagValue,
          "direct values must remain staged for one cycle");
    system.Step();
    Check(root->Sink().numberValue == 17 && root->Sink().flagValue,
          "int and bool queues must deliver values without shared_ptr wrapping");
    system.Reset();
    Check(root->Sink().numberValue == 0 && !root->Sink().flagValue,
          "system Reset must reset direct-value consumers");
}

} // namespace

int main()
{
    try {
        TestSimObjAndSimSys();
        TestDirectValueQueues();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
