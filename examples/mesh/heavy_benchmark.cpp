#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "MeshBench.h"

namespace {
using Clock = std::chrono::steady_clock;
using mesh_example::MeshBench;
using mesh_example::PacketTiming;

struct Options {
    std::vector<unsigned> iterations{0, 1000, 10000, 100000};
    std::vector<unsigned> workers{0, 1, 2, 4, 8};
    unsigned batches = 7;
    unsigned repeats = 2;
    unsigned warmup = 1;
    std::string output = "-";
};

unsigned Number(const std::string& value)
{
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("expected a nonnegative integer: " + value);
    const auto result = std::stoull(value);
    if (result > std::numeric_limits<unsigned>::max()) throw std::invalid_argument("integer too large");
    return static_cast<unsigned>(result);
}

std::vector<unsigned> Numbers(const std::string& value)
{
    std::vector<unsigned> result;
    std::istringstream input(value);
    std::string item;
    while (std::getline(input, item, ',')) {
        const unsigned number = Number(item);
        if (std::find(result.begin(), result.end(), number) != result.end())
            throw std::invalid_argument("duplicate value: " + item);
        result.push_back(number);
    }
    if (result.empty() || value.back() == ',') throw std::invalid_argument("empty list item");
    return result;
}

Options Parse(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 == argc) throw std::invalid_argument("missing option value");
        const std::string key = argv[i], value = argv[i + 1];
        if (key == "--iterations") options.iterations = Numbers(value);
        else if (key == "--workers") options.workers = Numbers(value);
        else if (key == "--batches") options.batches = Number(value);
        else if (key == "--repeats") options.repeats = Number(value);
        else if (key == "--warmup") options.warmup = Number(value);
        else if (key == "--output") options.output = value;
        else throw std::invalid_argument("unknown option: " + key);
    }
    if (!options.batches || !options.repeats) throw std::invalid_argument("batches and repeats must be positive");
    if (std::find(options.workers.begin(), options.workers.end(), 0) == options.workers.end())
        throw std::invalid_argument("include workers=0 as the serial baseline");
    return options;
}

struct Fixture {
    gfsim::SimSystem sim;
    std::shared_ptr<MeshBench> mesh;
    Fixture(unsigned workers, unsigned iterations)
        : mesh(std::make_shared<MeshBench>(&sim, false, iterations))
    {
        sim.GetLogger().SetLevel(gfsim::LogLevel::Off);
        sim.AddModule(mesh);
        if (workers) {
            sim.SetWorkerCount(workers);
            sim.SetParallelChildren(mesh.get());
        }
        sim.SetDeadlockThreshold(32);
        sim.Build();
    }
};

struct Reference {
    bool ready = false;
    std::uint64_t checksum = 0;
    std::array<PacketTiming, mesh_example::kPackets> timings;
    void Check(const Fixture& f, unsigned iterations)
    {
        GFSIM_ASSERT(f.sim.Cycles() == 132 && f.sim.NeedTerminate());
        GFSIM_ASSERT(f.mesh->StimuliSent() == 1560 && f.mesh->OutputsChecked() == 1560);
        GFSIM_ASSERT(f.mesh->TotalHops() == 6760);
        GFSIM_ASSERT(f.mesh->FakeRounds() == std::uint64_t(iterations) * mesh_example::kNodes * f.sim.Cycles());
        std::uint64_t latency = 0;
        for (std::size_t id = 0; id < f.mesh->Timings().size(); ++id) {
            const auto& t = f.mesh->Timings()[id];
            GFSIM_ASSERT(t.injected && t.delivered > t.injected);
            latency += t.delivered - t.injected;
            if (ready) GFSIM_ASSERT(t.injected == timings[id].injected && t.delivered == timings[id].delivered);
        }
        GFSIM_ASSERT(latency == 19246);
        if (ready) GFSIM_ASSERT(f.mesh->FakeChecksum() == checksum);
        else {
            checksum = f.mesh->FakeChecksum();
            timings = f.mesh->Timings();
            ready = true;
        }
    }
};

struct Row {
    unsigned iterations, workers, batch, repeat;
    double wallMs, cpuMs;
    std::uint64_t checksum, fakeRounds;
};

void Run(const Options& options)
{
    std::vector<Row> rows;
    for (unsigned iterations : options.iterations) {
        std::cerr << "fake ALU rounds/node/cycle=" << iterations << '\n';
        std::vector<std::unique_ptr<Fixture>> fixtures;
        Reference reference;
        for (unsigned workers : options.workers) {
            fixtures.emplace_back(new Fixture(workers, iterations));
            for (unsigned i = 0; i < options.warmup; ++i) {
                fixtures.back()->sim.Run(10000);
                reference.Check(*fixtures.back(), iterations);
                fixtures.back()->sim.Reset();
            }
        }
        for (unsigned batch = 0; batch < options.batches; ++batch) {
            // Rotate mode order to reduce systematic first/last-run bias.
            for (std::size_t position = 0; position < fixtures.size(); ++position) {
                const std::size_t index = (position + batch) % fixtures.size();
                auto& fixture = *fixtures[index];
                for (unsigned repeat = 0; repeat < options.repeats; ++repeat) {
                    fixture.sim.Reset();
                    const auto cpu = std::clock();
                    const auto start = Clock::now();
                    fixture.sim.Run(10000);
                    const double wallMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
                    const double cpuMs = 1000.0 * (std::clock() - cpu) / CLOCKS_PER_SEC;
                    reference.Check(fixture, iterations); // Verification is outside the timer.
                    rows.push_back(Row{iterations, options.workers[index], batch, repeat, wallMs, cpuMs,
                                       fixture.mesh->FakeChecksum(), fixture.mesh->FakeRounds()});
                }
            }
        }
    }
    std::ofstream file;
    std::ostream* output = &std::cout;
    if (options.output != "-") {
        file.open(options.output.c_str());
        if (!file) throw std::runtime_error("cannot open CSV output: " + options.output);
        output = &file;
    }
    *output << "iterations,workers,batch,repeat,wall_ms,cpu_ms,checksum,fake_rounds,cycles,packets,hops,total_latency\n"
            << std::fixed << std::setprecision(6);
    for (const auto& row : rows)
        *output << row.iterations << ',' << row.workers << ',' << row.batch << ',' << row.repeat << ','
                << row.wallMs << ',' << row.cpuMs << ',' << row.checksum << ',' << row.fakeRounds
                << ",132,1560,6760,19246\n";
    output->flush();
    if (!*output) throw std::runtime_error("failed to write benchmark CSV");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::cout << "Usage: mesh_heavy_benchmark [--iterations 0,1000,10000,100000]\n"
                     "  [--workers 0,1,2,4,8] [--batches 7] [--repeats 2] [--warmup 1] [--output FILE|-]\n"
                     "workers=0 uses serial traversal. iterations are host ALU rounds per node per cycle,\n"
                     "not simulated latency. Build Release and disable external tracing/profiling.\n";
        return 0;
    }
    try { Run(Parse(argc, argv)); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
