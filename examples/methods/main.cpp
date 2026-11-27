#include <iostream>
#include <stdexcept>
#include <string>
#include "MethodBench.h"

int main(int argc, char** argv)
{
    try {
        if (argc > 3) throw std::invalid_argument("usage: method_example [workers: 0|1|2|4] [topology.dot]");
        const std::string count = argc > 1 ? argv[1] : "0";
        if (count != "0" && count != "1" && count != "2" && count != "4")
            throw std::invalid_argument("workers must be 0, 1, 2, or 4 (0 means serial)");
        gfsim::SimSystem sim;
        auto bench = std::make_shared<method_example::MethodBench>(&sim);
        sim.AddModule(bench);
        if (count != "0") {
            sim.SetWorkerCount(static_cast<std::size_t>(std::stoul(count)));
            sim.SetParallelChildren(bench.get());
        }
        sim.GetLogger().SetLevel(gfsim::LogLevel::Debug);
        sim.SetDeadlockThreshold(2);
        sim.Build();
        if (argc > 2) sim.SaveDot(argv[2]);
        sim.Run(8);
        GFSIM_ASSERT(sim.NeedTerminate() && sim.Cycles() == 7);
        sim.ReportStat();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
