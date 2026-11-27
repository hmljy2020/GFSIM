#include <fstream>
#include <iostream>

#include "MeshBench.h"

int main(int argc, char** argv)
{
    if (argc != 1 && (argc != 3 || std::string(argv[1]) != "--timings")) {
        std::cerr << "Usage: mesh_example [--timings FILE]\n";
        return 1;
    }
    gfsim::SimSystem sim;
    auto mesh = std::make_shared<mesh_example::MeshBench>(&sim);
    sim.AddModule(mesh);
    sim.SetDeadlockThreshold(32);
    sim.Build();
    sim.Run(10000);
    sim.ReportStat();
    if (argc == 3) {
        std::ofstream output(argv[2]);
        output << "packet_id\tinjected_cycle\tdelivered_cycle\n";
        const auto& timings = mesh->Timings();
        for (std::size_t id = 0; id < timings.size(); ++id) {
            output << id << '\t' << timings[id].injected << '\t' << timings[id].delivered << '\n';
        }
        output.close();
        if (!output) {
            std::cerr << "Failed to write packet timings to " << argv[2] << '\n';
            return 1;
        }
    }
}
