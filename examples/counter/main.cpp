#include "CounterSystem.h"

int main()
{
    gfsim::SimSystem sim;
    sim.AddModule(std::make_shared<counter_example::CounterSystem>(&sim));
    sim.SetDeadlockThreshold(4);
    sim.GetLogger().SetLevel(gfsim::LogLevel::Info);
    sim.Build();
    sim.Run(10);
    sim.ReportStat();
}
