#include <functional>
#include <memory>
#include <vector>
#include "SimQueue.h"

using namespace gfsim;

namespace {
struct Packet {
    Packet(int v, RID r) : value(v), rid(r) {}
    const int value;
    RID rid;
};

template <typename Exception = AssertionError, typename F>
void Reject(F action)
{
    bool rejected = false;
    try { action(); } catch (const Exception&) { rejected = true; }
    GFSIM_ASSERT(rejected) << "expected failure before mutation";
}

void VisibleFront()
{
    SimQueue<int> q("visible");
    q.Write(10);
    q.Write(20);
    q.Work();
    const int& front = q.Front();
    q.Pop();
    GFSIM_ASSERT(!q.Empty() && q.Size() == 2 && q.Front() == 10 && q.Peek() == 10);
    GFSIM_ASSERT(&q.Front() == &front && front == 10);
    q.Work();
    GFSIM_ASSERT(q.Size() == 1 && q.Front() == 20);
    GFSIM_ASSERT(q.Read() == 20 && !q.Empty() && q.Front() == 20);
    q.Work();
    GFSIM_ASSERT(q.Empty());
    q.Work();
    GFSIM_ASSERT(q.Empty()) << "consumption commits exactly once";

    SimQueue<Packet> packets("handles");
    auto a = std::make_shared<Packet>(1, RID(1, false));
    auto b = std::make_shared<Packet>(2, RID(2, false));
    packets.Write(a);
    packets.Write(b);
    packets.Work();
    const auto& handle = packets.Front();
    const auto& payload = packets.Peek();
    const auto result = packets.Read();
    GFSIM_ASSERT(result == a && handle == a && packets.Front() == a);
    GFSIM_ASSERT(&payload == a.get() && payload.value == 1 && packets.Size() == 2);
    packets.Work();
    GFSIM_ASSERT(packets.Front() == b && result == a && result->value == 1);
}

void InvalidConsumption()
{
    // All four Pop/Read combinations, with one and multiple visible entries.
    for (unsigned count : {1u, 2u}) {
        for (bool firstRead : {false, true}) {
            for (bool secondRead : {false, true}) {
                SimQueue<Packet> q("duplicate");
                auto a = std::make_shared<Packet>(1, RID(1, false));
                auto b = std::make_shared<Packet>(2, RID(2, false));
                Reject([&] { q.Pop(); });
                Reject([&] { q.Read(); });
                q.Write(a);
                if (count == 2) q.Write(b);
                q.Work();
                if (firstRead) GFSIM_ASSERT(q.Read() == a);
                else q.Pop();
                Reject([&] { if (secondRead) q.Read(); else q.Pop(); });
                GFSIM_ASSERT(q.Size() == count && q.Front() == a && q.Peek().value == 1);
                q.Work();
                GFSIM_ASSERT(q.Size() == count - 1);
                if (count == 2) {
                    GFSIM_ASSERT(q.Read() == b);
                    q.Work();
                }
                GFSIM_ASSERT(q.Empty());
            }
        }
    }
}

void AdmissionLatencyAndStall()
{
    for (unsigned latency : {0u, 1u, 3u}) {
        SimQueue<int> q("latency");
        q.InitMaxSize(2);
        q.Setlatency(0);
        q.Write(1);
        q.Work();
        q.Setlatency(latency);
        q.Write(2);
        q.Pop();
        GFSIM_ASSERT(q.Front() == 1 && q.Size() == 1 && q.SizeW() == 1 && q.Full());
        Reject([&] { q.Write(3); });
        q.setStall();
        q.Work();
        GFSIM_ASSERT(q.Empty() && q.SizeW() == 1 && !q.Full());
        GFSIM_ASSERT(q.GetRawDelayCycle().front() == latency);
        q.Work();
        GFSIM_ASSERT(q.Empty() && q.GetRawDelayCycle().front() == latency);
        q.unsetStall();
        const unsigned boundaries = latency == 0 ? 1 : latency;
        for (unsigned i = 1; i <= boundaries; ++i) {
            q.Work();
            GFSIM_ASSERT(q.Size() == (i == boundaries ? 1u : 0u));
            if (i < boundaries) GFSIM_ASSERT(q.GetRawDelayCycle().front() == latency - i);
        }
        GFSIM_ASSERT(q.Read() == 2);
        q.Work();
        q.Work();
        GFSIM_ASSERT(q.WRBothEmpty());
    }
    SimQueue<int> full("full");
    full.InitMaxSize(1);
    full.Write(1);
    full.Work();
    full.Pop();
    GFSIM_ASSERT(full.Full() && !full.Empty());
    Reject([&] { full.Write(2); });
    full.Work();
    GFSIM_ASSERT(full.Empty() && !full.Full());
    full.Write(2);
    full.Work();
    GFSIM_ASSERT(full.Read() == 2);

    // A replacement staged before or after consumption sees the same budget.
    for (bool popFirst : {false, true}) {
        SimQueue<int> q("simultaneous");
        q.InitMaxSize(2);
        q.Write(1);
        q.Work();
        if (popFirst) q.Pop();
        q.Write(2);
        if (!popFirst) q.Pop();
        GFSIM_ASSERT(q.Front() == 1 && q.Full());
        q.Work();
        GFSIM_ASSERT(q.Size() == 1 && q.Front() == 2 && !q.Full());
    }
}

void ResetPending()
{
    SimQueue<int> q("reset");
    q.InitMaxSize(2);
    q.Setlatency(0);
    q.Write(1);
    q.Work();
    q.Read();
    q.Write(2);
    q.setStall();
    q.Reset();
    GFSIM_ASSERT(q.WRBothEmpty() && !q.getStall() && !q.Full(1));
    q.Write(3);
    q.Work();
    GFSIM_ASSERT(q.Front() == 3 && q.Size() == 1);
    q.Pop();
    q.Work();
    GFSIM_ASSERT(q.Empty());
}

void MaintenanceFlush()
{
    SimQueue<Packet> q("flush");
    q.InitMaxSize(5);
    q.SetTraceFormatter([](const Packet& p) { return std::to_string(p.value); });
    const RID target(1, false), other(2, false);
    auto a = std::make_shared<Packet>(1, target);
    auto b = std::make_shared<Packet>(2, other);
    q.Write(a);
    q.Write(b);
    q.Work();
    q.Setlatency(3);
    q.Write(a);
    q.Setlatency(2);
    q.Write(b);
    q.Pop();
    const auto before = q.Snapshot();
    unsigned calls = 0;
    Reject([&] { q.FlushIf([&](const std::shared_ptr<Packet>&) { ++calls; return true; }); });
    Reject([&] { q.FlushIf(target); });
    Reject([&] { q.FlushIf(target, [&](const RID&, const RID&) { ++calls; return true; }); });
    Reject<std::invalid_argument>([&] { q.FlushIf(RID()); });
    Reject<std::invalid_argument>([&] { q.FlushIf(RID(), [](const RID&, const RID&) { return true; }); });
    const auto after = q.Snapshot();
    GFSIM_ASSERT(calls == 0 && before.visible == after.visible && before.staged == after.staged);
    GFSIM_ASSERT(before.delays == after.delays && q.Front() == a && q.Size() == 2);
    q.Work(); // consumes a; staged delays become [2, 1]
    GFSIM_ASSERT(q.FlushIf(target) == 1);
    GFSIM_ASSERT(q.Front() == b && q.SizeW() == 1 && q.GetRawDelayCycle().front() == 1);
    q.Work();
    GFSIM_ASSERT(q.Size() == 2 && q.SizeW() == 0 && q.GetRawDelayCycle().empty());
    GFSIM_ASSERT(q.FlushIf(other) == 2 && q.Empty());
}
} // namespace

int main()
{
    VisibleFront();
    InvalidConsumption();
    AdmissionLatencyAndStall();
    ResetPending();
    MaintenanceFlush();
}
