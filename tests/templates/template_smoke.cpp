#include <memory>
#include <sstream>
#include <string>

#include "TemplateSmoke.h"
#include "SimAssert.h"

int main()
{
    gfsim::SimSys system;
    std::ostringstream report;
    std::ostringstream log;
    system.GetReporter().SetOutput(report);
    system.GetLogger().SetOutput(log);
    system.GetLogger().SetLevel(gfsim::LogLevel::Debug);
    auto module = std::make_shared<gfsim::TemplateSmoke>(&system);
    system.AddModule(module);
    system.Build();
    GFSIM_ASSERT(module->Objects().empty() && module->Submodules().empty());
    for (int pass = 0; pass < 2; ++pass) {
        for (unsigned cycle = 0; cycle <= 3; ++cycle) {
            if (cycle != 0) system.Step();
            report.str("");
            system.ReportStat();
            GFSIM_ASSERT(report.str().find("TemplateSmoke Stats") != std::string::npos);
            const std::size_t label = report.str().find("Work cycles");
            GFSIM_ASSERT(label != std::string::npos);
            const std::size_t separator = report.str().find(':', label);
            GFSIM_ASSERT(separator != std::string::npos);
            std::istringstream value(report.str().substr(separator + 1));
            std::uint64_t reportedCount = 0;
            value >> reportedCount;
            GFSIM_ASSERT(!value.fail() && reportedCount == cycle)
                << "incorrect committed cycle count: " << report.str();
            if (cycle != 0) {
                const std::string line = "[C:" + std::to_string(cycle) +
                    "][TemplateSmoke.work][DEBUG] work_cycles=" + std::to_string(cycle) + "\n";
                GFSIM_ASSERT(log.str().find(line) != std::string::npos) << line;
            }
        }
        system.Reset();
        log.str("");
    }
    // The template's bookkeeping counter alone never claims useful progress.
    system.SetDeadlockThreshold(2);
    bool deadlocked = false;
    try { system.Run(3); }
    catch (const gfsim::DeadlockError&) { deadlocked = true; }
    GFSIM_ASSERT(deadlocked && system.Cycles() == 2);
}
