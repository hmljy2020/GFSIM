# RID and queue flush API

Header: `include/RID.h`. Namespace: `gfsim`. Requires C++11.

`RID` is a small identity record with `value` (`uint32_t`), `wrap`, and `valid`. Default construction produces an invalid RID; `RID(index, wrapped)` produces a valid one. Equality compares all three fields, so an index reused after wrap is a different identity.

`SimQueue<T>` offers three flush forms:

| Call | Behavior |
| --- | --- |
| `FlushIf(rid)` | Remove packets whose RID exactly matches the valid target. |
| `FlushIf(rid, compare)` | Remove packets for which `compare(packetRid, targetRid)` returns true. Invalid packet RIDs are ignored. |
| `FlushIf(predicate)` | Preserve the generic packet predicate path for non-RID criteria, including thread or block context. |

Flush is immediate maintenance: use it only while the system is quiescent, with exclusive queue access and no pending Pop/Read. A pending consumption raises `AssertionError` before predicate evaluation or mutation; invalid target RID validation still takes precedence.

All forms return the number of removed packets. They inspect both staged and visible storage. Staged delay counters are removed with their corresponding packets, including when latency changes between writes.

For class/struct `T`, the default RID accessor reads a field declared as `RID rid`. If the identity is nested or computed, call `SetRidExtractor(std::function<RID(const T&)>)`. Scalar queues also need an extractor before RID-based flushing. The extractor receives the **value/object**, not the shared pointer.

A bare RID does not define ordering across threads, blocks, or ring generations. Use the comparator overload for an explicitly defined age rule, or the generic predicate when the flush requires additional context. Invalid target RIDs raise `std::invalid_argument`; a missing extractor raises `std::logic_error` when packets are inspected.

The [RID flush test](../../tests/queue/test_flush_rid.cpp) covers staged and visible packets, wrap distinction, mixed latencies, a custom extractor, and a custom comparator.
