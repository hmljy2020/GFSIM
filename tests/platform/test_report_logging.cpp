#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include "SimModule.h"

namespace {

void Check(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void TestReporter()
{
    std::ostringstream output;
    gfsim::Reporter reporter(output);
    reporter.ReportTitle("Queue Stats");
    reporter.ReportVal("Accepted", 7);
    reporter.ReportVal("Latency", 1.234, 3);
    reporter.ReportAvg("Average", 3, 2);
    reporter.ReportPct("Busy", 1, 4);
    reporter.ReportValAndPct("Stalled", 1, 4);
    reporter.ReportAvg("No samples", 0, 0);
    reporter.ReportPct("No cycles", 0, 0);

    const std::string report = output.str();
    Check(report.find("Queue Stats") != std::string::npos, "title must be present");
    Check(report.find("Accepted") != std::string::npos &&
              report.find(":         7") != std::string::npos,
          "integer value must be formatted");
    Check(report.find("1.234") != std::string::npos,
          "floating value precision must be respected");
    Check(report.find("Average") != std::string::npos &&
              report.find("1.50") != std::string::npos,
          "average must be formatted");
    Check(report.find("25.00%") != std::string::npos &&
              report.find("1 (25.00%)") != std::string::npos,
          "percent and combined value must be formatted");
    Check(report.find("N/A") != std::string::npos,
          "zero denominators must be reported without division");

    std::ostringstream alternate;
    reporter.SetOutput(alternate);
    reporter.ReportVal("Redirected", 2);
    Check(alternate.str().find("Redirected") != std::string::npos,
          "report sink must be replaceable");
}

void TestLogger()
{
    std::ostringstream output;
    gfsim::Logger logger(output);
    Check(logger.Level() == gfsim::LogLevel::Warn,
          "default log threshold must be Warn");
    logger.SetLevel(gfsim::LogLevel::Info);
    logger.SetCycle(3);
    Check(logger.Cycle() == 3, "logger cycle must be readable");
    logger.Debug("fetch", "issue") << "filtered";
    logger.Info("fetch", "issue") << "value=" << std::hex << 26;
    logger.Warn("memory", "wait") << "busy";
    const std::string logged = output.str();
    Check(logged.find("filtered") == std::string::npos,
          "messages below threshold must be filtered");
    Check(logged.find("[C:3][fetch.issue][INFO] value=1a\n") != std::string::npos,
          "log line must include cycle, unit, stage, level and stream formatting");
    Check(logged.find("[C:3][memory.wait][WARN] busy\n") != std::string::npos,
          "warning must be emitted");

    logger.SetLevel(gfsim::LogLevel::Off);
    logger.Error("memory", "wait") << "hidden";
    Check(output.str().find("hidden") == std::string::npos,
          "Off must suppress all messages");

    std::ostringstream alternate;
    logger.SetOutput(alternate);
    logger.SetLevel(gfsim::LogLevel::Error);
    logger.Error("core", "run") << "failed";
    Check(alternate.str().find("[ERROR] failed") != std::string::npos,
          "log sink must be replaceable");
}

class ReportingModule final : public gfsim::SimModule {
public:
    explicit ReportingModule(gfsim::SimSystem* system)
        : SimModule("reporting", system) {}

    void Build() override {}

    void Work() override
    {
        ++processed_;
        Log().Info("module", "work") << "processed=" << processed_;
    }

    void Xfer() override {}
    void Reset() override { processed_ = 0; }

    void ReportStat() override
    {
        auto& report = Report();
        report.ReportTitle("Module Stats");
        report.ReportVal("Processed", processed_);
    }

private:
    int processed_ = 0;
};

void TestSystemIntegration()
{
    gfsim::SimSys system;
    std::ostringstream reportOutput;
    std::ostringstream logOutput;
    system.GetReporter().SetOutput(reportOutput);
    system.GetLogger().SetOutput(logOutput);
    system.GetLogger().SetLevel(gfsim::LogLevel::Info);
    system.AddModule(std::make_shared<ReportingModule>(&system));
    system.Build();

    system.Step();
    system.ReportStat();
    Check(logOutput.str().find("[C:1][module.work][INFO] processed=1") !=
              std::string::npos,
          "system must stamp module logs with its current cycle");
    Check(reportOutput.str().find("Module Stats") != std::string::npos &&
              reportOutput.str().find("Processed") != std::string::npos,
          "system must route ReportStat through its reporter");

    system.Reset();
    Check(system.GetLogger().Cycle() == 0,
          "reset must reset the logger cycle");
    system.Step();
    Check(logOutput.str().find("[C:1][module.work][INFO] processed=1", 1) !=
              std::string::npos,
          "module state and log cycle must restart after reset");
}

} // namespace

int main()
{
    try {
        TestReporter();
        TestLogger();
        TestSystemIntegration();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
