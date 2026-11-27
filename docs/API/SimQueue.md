# SimQueue API

Header: `include/SimQueue.h`. Namespace: `gfsim`. Requires C++11.

`SimQueue<T>` uses a type-based message policy with a clear boundary:

| `T` | Stored packet type | Example |
| --- | --- | --- |
| Arithmetic or enum | `T` by value | `SimQueue<int>`, `SimQueue<bool>` |
| Class or struct | `std::shared_ptr<T>` | `SimQueue<Request>` |

Raw pointers and `SimQueue<std::shared_ptr<T>>` are rejected. A producer allocates a new class message with `std::make_shared<T>()`. A forwarding module peeks with `Front()`, writes the shared handle to the next queue, then pops the input after the write succeeds. Use `packet->field` to access the object. Scalar tokens follow the same admission order but are copied by value.

## Construction, capacity, and phases

| Method | Contract |
| --- | --- |
| `SimQueue(std::string name = "SimQueue")` / `Name()` | Construct and identify a queue. Give every physical link a descriptive name. |
| `Build()` | No-op reserved for queue setup. |
| `Work()` | Commit one pending consumption, refresh the next-cycle write budget, then (unless stalled) decrement delays and publish ready staged entries. |
| `Xfer()` | No-op; queue `Work()` is the legacy advancement operation, invoked during system Xfer by the owner helper or assigned worker task. |
| `Reset()` | Clear staged, visible, delay, and pending-consumption state; release the stall. Capacity and latency remain. |
| `Setlatency(cycles)` | Set delay for later writes; values 0 and 1 transfer on the next queue `Work()`. |
| `InitMaxSize(size)` | Set capacity in the range **1..512**. Zero, values above 512, or values below current occupancy raise `AssertionError`. Default is 512. |
| `setStall()` / `unsetStall()` / `getStall()` | Control and inspect transfer suspension. |

`SimQueue<T>` is physically owned through a parent module's `AddInnerQueue()`. `SimSystem::Build()` requires one `BindOutput()` producer and one `BindInput()` consumer for each queue. An unconnected queue or a queue registered at system root fails build. Cyclic connections are valid.

## Data access

| Method | Contract |
| --- | --- |
| `Write(Packet packet)` | Stage a value or non-null shared pointer. Full writes fail before mutation. |
| `Packet Read()` | Copy the visible front scalar/shared handle and request consumption at the boundary; never move out the visible handle. |
| `const Packet& Front() const` | Inspect the visible front. Pop/Read requests preserve the reference throughout Work; do not retain it across advancement, flush, or reset. |
| `const T& Peek() const` | Inspect the scalar value or referenced class object through a const view suitable for `PURE` methods; the view survives a Pop/Read request, but do not retain it across advancement, flush, or reset without independent payload ownership. |
| `Pop()` | Request consumption of the visible front at the boundary, without returning it. |
| `Empty()` / `WRBothEmpty()` | Check visible storage only / both storage deques. |
| `Full()` / `Full(reserve)` / `toBeOverflow(reserve)` | Check the remaining write budget for this cycle. For `n` writes, require `!Full(n - 1)`. |
| `Size()` / `SizeW()` / `MaxSize()` | Return visible count / staged count / configured capacity. |

Every queue allows **at most one successful Pop or Read per cycle**, by its
single consumer. Empty consumption and a second consumption request (including
mixed Pop/Read calls) raise `AssertionError` before changing queue state. All
visible queries still inspect the cycle-start deque: after requesting Pop on
`[A, B]`, Front/Peek still return A and Size is two. After queue advancement,
they return B and one. A single visible entry remains nonempty until advancement.
Do not drain with `while (!Empty()) Pop()` or repeatedly call Read in one Work.
Use one transaction per cycle, and use completed-transaction counts or a later
Work query for completion. Do not move queue checks into module Xfer.

At the start of a cycle, the write budget is capacity minus both visible and
staged entries. Each successful Write spends one slot. Pop/Read do not change
this budget or the visible deque during Work. A depth-one queue that starts full
remains nonempty and full after a consumption request. Queue advancement first
commits consumption, then refreshes the budget, even when stalled; stall blocks
only delay advancement and publication. Maintenance FlushIf removes entries
immediately but likewise returns capacity only at the next boundary. Reset
restores the full budget. See [decision 0012](../decisions/0012-queue-write-admission.md)
and its consumption update in [decision 0019](../decisions/0019-delayed-queue-consumption.md).

`GetRawWriteData()` and `GetRawReadData()` expose mutable `std::deque<Packet>&`;
`GetRawDelayCycle()` exposes delay metadata. These are exclusive maintenance
interfaces, not module transfers. Never mutate visible storage during Work or
with a consumption pending. Direct mutation bypasses capacity and delay
bookkeeping. `FlushIf`, `MatchIf`, and `MatchCount` visit both deques and pass
the stored packet type to predicates.

## RID recovery

`FlushIf(RID)` removes an exact RID from both staged and visible storage. `FlushIf(RID, compare)` applies a caller-defined RID policy; `FlushIf(predicate)` remains available for full packet/context matching. All return a removed-packet count and keep staged delay entries aligned. A class packet may expose `RID rid`, or the queue may use `SetRidExtractor()` when the RID is stored elsewhere. See the [RID API](RID.md).

FlushIf is immediate maintenance only: the system must be quiescent, access
exclusive, and no consumption pending. Every overload asserts `!popPending_`
before invoking predicates or changing storage; RID overloads still validate the
target first. It is not a deferred recovery request. The guard does not detect
concurrent callers or enforce quiescence.

## Trace

`SetTraceFormatter(std::function<std::string(const T&)>)` formats the value or referenced object, and `Snapshot()` returns all visible/staged payload strings, delays, capacity, and stall state. Streamable `T` uses `operator<<` by default; other types need an explicit formatter. See the [trace API](SimTrace.md).

The [topology contract test](../../tests/graph/test_topology_contract.cpp) checks direct `int`/`bool` packets, shared pointer class packets, fixed capacity, and legal cyclic connections. The [pipeline test](../../tests/pipeline/test_three_stage_pipeline.cpp) shows one class-message pointer moving through three stages without replacing the object.

## Parallel access contract

Queue synchronization relies on the system's Work/Xfer barriers and the
validated single producer/single consumer topology, rather than queue mutexes.
During Work, the producer alone calls `Full`, `toBeOverflow`, `Write`, or
`SizeW`; the consumer alone calls `Pop` or `Read` under the admission rules.
Other threads may query `Empty`, `Size`, `Front`, and `Peek`, including through
ordinary bound-module methods. These read-only queries do not access the
consumption flag. Producer writes modify staged storage/write occupancy; the
consumer sets a plain private bool. Neither changes visible storage during Work.
A pop does not free same-cycle write capacity. Queue `Work()` is advancement,
called exactly once during system Xfer, by the owner helper or delegated worker
task. Worker assignment follows the producer subtree, falling back to the
consumer subtree; queues with neither endpoint on a worker stay on owner traversal.
Normal module Work must never advance a queue.

FlushIf requires quiescence, exclusive access, and no pending consumption.
The following other interfaces require quiescence or a proven single-thread
ordering covering both endpoints and the advancement task: `WRBothEmpty`, `MatchIf`, `MatchCount`,
`Snapshot`, all raw deque access, direct advancement/reset, stall changes,
latency/capacity changes, and formatter/RID-extractor replacement. The APIs do
not dynamically enforce this restriction. Const capacity/configuration reads
are safe while configuration is fixed. Module Xfer must not query queue state:
physical ownership no longer determines publication order, and a phase barrier
before Xfer does not order operations within Xfer. Check Full/Empty and other
readiness conditions during module Work. Main-thread trace capture runs after
all Xfer tasks finish.

Shared pointers preserve message identity, not permission for concurrent
mutation. After successful admission, the forwarding stage may mutate a
packet only while it has exclusive logical access. Read-only observers can
still see that packet in the input after Pop, so a model using concurrent
payload queries must prohibit concurrent payload mutation, even after output
admission. Visible structure stability is not a deep copy or immutable-payload
guarantee. Do not retain mutable aliases, fan out a
mutable packet to concurrently active consumers, or mutate visible payloads
from a producer. Inspect readiness before compute and commit outputs before
popping inputs, including blocked cycles. Guest memory from `GetMemory()` has
no added locking: keep accesses in one sequential task, or supply application
synchronization and deterministic ordering for shared writes/mapping changes.

The [delayed consumption test](../../tests/queue/test_delayed_consume.cpp) covers
queue boundaries and failure paths; the [method visibility test](../../tests/queue/test_queue_methods.cpp)
compares immutable-payload observations cycle by cycle in serial and 1/2/4-worker runs.
