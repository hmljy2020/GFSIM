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
| `Work()` | Publish freed slots for the next cycle, even when stalled. Unless stalled, also decrement delays and move ready entries to visible storage. |
| `Xfer()` | No-op; the queue advances in `Work()`. Parent-owned inner queues are advanced by their parent in `Xfer()`. |
| `Reset()` | Clear staged, visible, and delay state; release the stall. Capacity and latency remain. |
| `Setlatency(cycles)` | Set delay for later writes; values 0 and 1 transfer on the next queue `Work()`. |
| `InitMaxSize(size)` | Set capacity in the range **1..512**. Zero, values above 512, or values below current occupancy raise `AssertionError`. Default is 512. |
| `setStall()` / `unsetStall()` / `getStall()` | Control and inspect transfer suspension. |

`SimQueue<T>` is physically owned through a parent module's `AddInnerQueue()`. `SimSystem::Build()` requires one `BindOutput()` producer and one `BindInput()` consumer for each queue. An unconnected queue or a queue registered at system root fails build. Cyclic connections are valid.

## Data access

| Method | Contract |
| --- | --- |
| `Write(Packet packet)` | Stage a value or non-null shared pointer. Full writes fail before mutation. |
| `Packet Read()` | Remove and return the visible front value/pointer. |
| `const Packet& Front() const` | Inspect the visible front without removal; the reference becomes invalid after a pop/read/reset. |
| `const T& Peek() const` | Inspect the scalar value or referenced class object through a const view suitable for `PURE` methods; do not retain the reference after pop/read/reset. |
| `Pop()` | Remove the visible front without returning it. |
| `Empty()` / `WRBothEmpty()` | Check visible storage only / both storage deques. |
| `Full()` / `Full(reserve)` / `toBeOverflow(reserve)` | Check the remaining write budget for this cycle. For `n` writes, require `!Full(n - 1)`. |
| `Size()` / `SizeW()` / `MaxSize()` | Return visible count / staged count / configured capacity. |

At the start of a cycle, the write budget is capacity minus both visible and staged entries. Each successful write spends one slot. Removing entries with `Pop()`, `Read()`, or `FlushIf()` changes the contents immediately, but frees capacity for the producer only at the next queue `Work()`. A depth-one queue that starts full can therefore be both `Empty()` and `Full()` after a pop: it has nothing left to read and no slot left to write this cycle. Reset restores the full budget. See [decision 0012](../decisions/0012-queue-write-admission.md).

`GetRawWriteData()` and `GetRawReadData()` expose mutable `std::deque<Packet>&`; `GetRawDelayCycle()` exposes delay metadata. Direct mutation bypasses normal capacity and delay bookkeeping; do not use these accessors for module transfers. `FlushIf`, `MatchIf`, and `MatchCount` visit both storage deques and pass the stored packet type to predicates.

## RID recovery

`FlushIf(RID)` removes an exact RID from both staged and visible storage. `FlushIf(RID, compare)` applies a caller-defined RID policy; `FlushIf(predicate)` remains available for full packet/context matching. All return a removed-packet count and keep staged delay entries aligned. A class packet may expose `RID rid`, or the queue may use `SetRidExtractor()` when the RID is stored elsewhere. See the [RID API](RID.md).

## Trace

`SetTraceFormatter(std::function<std::string(const T&)>)` formats the value or referenced object, and `Snapshot()` returns all visible/staged payload strings, delays, capacity, and stall state. Streamable `T` uses `operator<<` by default; other types need an explicit formatter. See the [trace API](SimTrace.md).

The [topology contract test](../../tests/graph/test_topology_contract.cpp) checks direct `int`/`bool` packets, shared pointer class packets, fixed capacity, and legal cyclic connections. The [pipeline test](../../tests/pipeline/test_three_stage_pipeline.cpp) shows one class-message pointer moving through three stages without replacing the object.

## Parallel access contract

Queue synchronization relies on the system's Work/Xfer barriers and the
validated single producer/single consumer topology, rather than queue mutexes.
During Work, the producer alone calls `Full`, `toBeOverflow`, `Write`, or
`SizeW`; the consumer alone calls `Empty`, `Size`, `Front`, `Peek`, `Pop`, or
`Read` (subject to the transaction admission rules). Producer writes modify
staged storage and write occupancy; consumer reads/pops modify visible storage.
A pop does not free same-cycle write capacity. Queue `Work()` is advancement,
called exactly once by the owning module's `AdvanceInnerQueues()` during Xfer.
Normal module Work must never advance a queue.

The following require quiescence or a proven single-thread ordering covering
both endpoints and the owner: `FlushIf`, `WRBothEmpty`, `MatchIf`, `MatchCount`,
`Snapshot`, all raw deque access, direct advancement/reset, stall changes,
latency/capacity changes, and formatter/RID-extractor replacement. The APIs do
not dynamically enforce this restriction. Const capacity/configuration reads
are safe while configuration is fixed. Xfer reads of another owner's queues
must not overlap that owner's advancement; a phase barrier before Xfer does
not order operations *within* Xfer. Parent advancement before dispatch is one
valid ordering. Main-thread trace capture runs after all Xfer tasks finish.

Shared pointers preserve message identity, not permission for concurrent
mutation. After successful admission, the forwarding stage may mutate a
packet only while it has exclusive logical access and the next consumer
cannot see it until the boundary. Do not retain mutable aliases, fan out a
mutable packet to concurrently active consumers, or mutate visible payloads
from a producer. Inspect readiness before compute and commit outputs before
popping inputs, including blocked cycles. Guest memory from `GetMemory()` has
no added locking: keep accesses in one sequential task, or supply application
synchronization and deterministic ordering for shared writes/mapping changes.
