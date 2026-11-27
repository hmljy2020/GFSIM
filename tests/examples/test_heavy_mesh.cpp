#include <map>
#include <sstream>
#include "MeshBench.h"

namespace {
using namespace mesh_example;
struct Fixture {
    gfsim::SimSystem sim;
    std::shared_ptr<MeshBench> bench;
    std::map<std::string, gfsim::SimQueue<Packet>*> queues;
    Fixture(unsigned workers, unsigned iterations)
        : bench(std::make_shared<MeshBench>(&sim, false, iterations))
    {
        sim.GetLogger().SetLevel(gfsim::LogLevel::Off);
        sim.AddModule(bench);
        if (workers) {
            sim.SetWorkerCount(workers);
            sim.SetParallelChildren(bench.get());
        }
        sim.SetDeadlockThreshold(32);
        sim.Build();
        for (auto* object : bench->Objects()) {
            if (auto* q = dynamic_cast<gfsim::SimQueue<Packet>*>(object)) queues.emplace(q->Name(), q);
        }
    }
};

void Compare(const Fixture& baseline, const Fixture& actual, bool fakeState)
{
    GFSIM_ASSERT(actual.sim.Cycles() == baseline.sim.Cycles());
    GFSIM_ASSERT(actual.sim.NeedTerminate() == baseline.sim.NeedTerminate());
    GFSIM_ASSERT(actual.bench->OutputsChecked() == baseline.bench->OutputsChecked());
    GFSIM_ASSERT(actual.bench->TotalHops() == baseline.bench->TotalHops());
    if (fakeState) GFSIM_ASSERT(actual.bench->FakeChecksum() == baseline.bench->FakeChecksum());
    for (int id = 0; id < kPackets; ++id) {
        const auto& a = actual.bench->Timings()[id];
        const auto& b = baseline.bench->Timings()[id];
        GFSIM_ASSERT(a.injected == b.injected && a.delivered == b.delivered);
    }
    for (const auto& entry : baseline.queues) {
        const auto a = actual.queues.at(entry.first)->Snapshot();
        const auto b = entry.second->Snapshot();
        GFSIM_ASSERT(a.visible == b.visible && a.staged == b.staged && a.delays == b.delays);
    }
}

void Equivalence(unsigned iterations)
{
    Fixture light(0, 0), serial(0, iterations), one(1, iterations), two(2, iterations), four(4, iterations);
    std::vector<Fixture*> heavy{&serial, &one, &two, &four};
    const auto initial = serial.bench->FakeChecksum();
    std::uint64_t expected = 0;
    for (int pass = 0; pass < 2; ++pass) {
        while (!light.sim.NeedTerminate()) {
            light.sim.Step();
            for (auto* f : heavy) {
                f->sim.Step();
                Compare(light, *f, false);
                GFSIM_ASSERT(f->bench->FakeRounds() == std::uint64_t(iterations) * kNodes * f->sim.Cycles());
            }
            for (auto* f : heavy) Compare(serial, *f, true);
        }
        GFSIM_ASSERT(light.sim.Cycles() == 132 && light.bench->OutputsChecked() == 1560);
        if (iterations) GFSIM_ASSERT(serial.bench->FakeChecksum() != initial);
        if (pass == 0) expected = serial.bench->FakeChecksum();
        else GFSIM_ASSERT(serial.bench->FakeChecksum() == expected);
        std::ostringstream report;
        serial.sim.GetReporter().SetOutput(report);
        serial.sim.ReportStat();
        if (iterations) GFSIM_ASSERT(report.str().find("Total fake ALU rounds (since reset)") != std::string::npos);
        light.sim.Reset();
        for (auto* f : heavy) {
            f->sim.Reset();
            GFSIM_ASSERT(f->bench->FakeRounds() == 0 && f->bench->FakeChecksum() == initial);
        }
    }
}

void FakeWorkDoesNotHideDeadlock()
{
    Fixture f(4, 257);
    for (int i = 0; i < kNodes; ++i) f.queues.at("inject_" + std::to_string(i))->setStall();
    bool deadlocked = false;
    try { f.sim.Run(1000); } catch (const gfsim::DeadlockError&) { deadlocked = true; }
    GFSIM_ASSERT(deadlocked && f.sim.StalledCycles() == 32 && f.bench->OutputsChecked() == 0);
    GFSIM_ASSERT(f.bench->FakeRounds() == std::uint64_t(257) * kNodes * f.sim.Cycles());
    f.sim.Reset();
    GFSIM_ASSERT(f.bench->FakeRounds() == 0);
    f.sim.Run(1000);
    GFSIM_ASSERT(f.sim.Cycles() == 132 && f.bench->OutputsChecked() == 1560);
}
} // namespace

int main()
{
    for (unsigned iterations : {0u, 17u, 257u}) Equivalence(iterations);
    FakeWorkDoesNotHideDeadlock();
}
