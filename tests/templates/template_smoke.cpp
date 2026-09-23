#include <memory>
#include <sstream>
#include <string>

#include "TemplateSmoke.h"

int main()
{
    gfsim::SimSys system;
    std::ostringstream report;
    std::ostringstream log;
    system.GetReporter().SetOutput(report);
    system.GetLogger().SetOutput(log);
    system.GetLogger().SetLevel(gfsim::LogLevel::Debug);
    system.AddModule(std::make_shared<gfsim::TemplateSmoke>(&system));
    system.Build();
    system.Step();
    system.ReportStat();

    return report.str().find("Work cycles") != std::string::npos &&
                   report.str().find("1") != std::string::npos &&
                   log.str().find("[C:1][TemplateSmoke.work][DEBUG]") !=
                       std::string::npos
               ? 0 : 1;
}
