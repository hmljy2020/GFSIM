#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "SimQueue.h"

namespace {

struct Packet {
    Packet(gfsim::RID identity, int payload)
        : rid(identity), value(payload) {}
    gfsim::RID rid;
    int value;
};

struct NestedPacket {
    explicit NestedPacket(gfsim::RID identity) : identity(identity) {}
    gfsim::RID identity;
};

void Check(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void TestExactRidAndDelayAlignment()
{
    gfsim::SimQueue<Packet> queue("requests");
    queue.InitMaxSize(8);
    queue.Setlatency(2);
    const gfsim::RID rid(7, false);
    const gfsim::RID wrapped(7, true);
    const gfsim::RID other(9, false);
    queue.Write(std::make_shared<Packet>(rid, 1));
    const auto survivor = std::make_shared<Packet>(other, 2);
    queue.Write(survivor);
    queue.Write(std::make_shared<Packet>(rid, 3));
    queue.Work(); // all three remain staged with one delay cycle left
    queue.Setlatency(1);
    const auto wrappedPacket = std::make_shared<Packet>(wrapped, 4);
    queue.Write(wrappedPacket);

    Check(queue.FlushIf(rid) == 2 && queue.SizeW() == 2 &&
              queue.GetRawDelayCycle().size() == queue.SizeW() &&
              queue.GetRawDelayCycle().front() == 1,
          "exact RID flush must remove staged matches and aligned delays");
    queue.Work();
    Check(queue.Size() == 2 && queue.SizeW() == 0 &&
              queue.GetRawDelayCycle().empty() && queue.Front() == survivor,
          "surviving packets must keep FIFO order and correct latency");

    queue.Write(std::make_shared<Packet>(wrapped, 5)); // staged match
    Check(queue.FlushIf(wrapped) == 2 && queue.Size() == 1 &&
              queue.SizeW() == 0 && queue.GetRawDelayCycle().empty() &&
              queue.Read() == survivor,
          "RID wrap bit must distinguish identities across both queue sides");

    bool rejected = false;
    try {
        queue.FlushIf(gfsim::RID{});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Check(rejected, "invalid RID must not silently flush packets");
}

void TestExtractorAndComparator()
{
    const gfsim::RID first(5, false);
    const gfsim::RID cutoff(7, false);
    const gfsim::RID later(9, false);
    gfsim::SimQueue<NestedPacket> withoutExtractor("missing_extractor");
    withoutExtractor.Write(std::make_shared<NestedPacket>(first));
    bool missingRejected = false;
    try {
        withoutExtractor.FlushIf(first);
    } catch (const std::logic_error&) {
        missingRejected = true;
    }
    Check(missingRejected && withoutExtractor.SizeW() == 1 &&
              withoutExtractor.GetRawDelayCycle().size() == 1,
          "missing RID extractor must fail without consuming packets");

    gfsim::SimQueue<NestedPacket> queue("nested");
    queue.InitMaxSize(4);
    queue.SetRidExtractor([](const NestedPacket& packet) { return packet.identity; });
    queue.Write(std::make_shared<NestedPacket>(first));
    queue.Write(std::make_shared<NestedPacket>(cutoff));
    queue.Write(std::make_shared<NestedPacket>(later));
    queue.Work();
    const auto atOrAfter = [](const gfsim::RID& packet, const gfsim::RID& target) {
        return packet.wrap == target.wrap && packet.value >= target.value;
    };
    Check(queue.FlushIf(cutoff, atOrAfter) == 2 && queue.Size() == 1 &&
              queue.Front()->identity == first,
          "caller-supplied RID comparison must choose the flush policy");

    gfsim::SimQueue<int> scalar("scalar");
    scalar.SetRidExtractor([](const int& value) {
        return gfsim::RID(static_cast<std::uint32_t>(value), false);
    });
    scalar.Write(5);
    scalar.Work();
    Check(scalar.FlushIf(first) == 1 && scalar.Empty(),
          "RID extractor must also support value payloads");
}

void TestGenericPredicate()
{
    gfsim::SimQueue<Packet> queue("predicate");
    queue.Write(std::make_shared<Packet>(gfsim::RID(1, false), 10));
    queue.Write(std::make_shared<Packet>(gfsim::RID(2, false), 20));
    auto match = [](const std::shared_ptr<Packet>& packet) {
        return packet->value == 10;
    };
    Check(queue.FlushIf(match) == 1 && queue.SizeW() == 1 &&
              queue.GetRawDelayCycle().size() == 1,
          "predicate FlushIf must remain available and keep delay metadata aligned");
}

} // namespace

int main()
{
    try {
        TestExactRidAndDelayAlignment();
        TestExtractorAndComparator();
        TestGenericPredicate();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
