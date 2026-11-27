# 0019: Commit queue consumption at the phase boundary

## Problem and decision

Immediate Pop/Read changed visible queue structure during module Work. A
read-only method on another module could observe a different head depending on
execution order, and concurrent queries could race with deque removal. This
decision supersedes the immediate-removal portion of decision 0012 while retaining
its next-cycle write admission policy.

SimQueue keeps one ordinary `bool popPending_`, initially false. Pop validates
nonempty visible storage and no pending consumption, then sets the flag. Read
performs the same checks, copies the front scalar or shared handle, and sets the
flag; it never moves out the visible handle. Each queue permits at most one
successful Pop/Read per cycle. Every duplicate combination raises GFSIM_ASSERT
before mutation, even with multiple visible entries. There is no batch interface,
consumption cursor, deque snapshot, lock, atomic flag, or new scheduler task.

Front/Peek/Empty/Size continue reading readQ directly and never inspect the flag.
For `[A, B]`, a consumption request leaves the same head A and size two throughout
Work; the next Work sees B and size one. One visible entry likewise remains
nonempty until the boundary. This keeps visible structure stable, not the shared
payload: concurrent payload queries require the model to exclude mutation.

## Advancement, capacity, and maintenance

The existing queue Work operation runs during system Xfer and performs:

1. Commit a pending consumption by removing one visible head and clearing the flag.
2. Refresh writeOccupancy from visible plus staged storage.
3. Return if stalled.
4. Otherwise decrement delays and publish staged entries in the existing FIFO order.

A consumption request never frees same-cycle write capacity. Stall prevents only
delay advancement and publication, not consumption or capacity refresh. Reset
clears both deques, delays, occupancy, and the consumption flag, and releases stall.
Latency zero and one still publish at the next advancement; larger delays retain
their prior meaning.

FlushIf remains immediate maintenance, restricted to a quiescent system with
exclusive access and no pending consumption. The predicate path asserts that no
consumption is pending before any predicate call or modification. RID overloads
still validate their target before delegating. Valid maintenance flushes preserve
packet/delay alignment and return capacity at the next boundary. The flag guard
does not dynamically enforce quiescence or exclusive access.

Raw deque interfaces remain available for exclusive maintenance; never mutate
visible storage in Work or while consumption is pending. Queries involving staged
storage retain their existing exclusive-access/ordering contract.

## Parallel access and migration

The single producer owns Write and capacity queries. The single consumer owns
Pop/Read. Other workers may query Front/Peek/Empty/Size during Work through ordinary
module methods; they only read visible storage. Both existing barriers, worker
planning, and producer/consumer-based queue advancement affinity stay unchanged.
No module may query a queue or another module's state during Xfer.

Front/Peek references survive a consumption request but should not cross queue
advancement, maintenance flush, or reset. An independently retained shared handle
keeps its payload alive, without permitting concurrent mutation. Models that
previously drained a queue with repeated Read/Pop must consume one entry per
cycle. Completion should use completed-transaction counts or a later Work query.

Mesh already snapshots one requested output per input before arbitration, so it
retains its arbitration order and generated logic. A regression with two heads
requesting different outputs checks one consumption per input per cycle. The
pipeline test moves sink checks from Xfer into Work and therefore completes on
cycle five instead of four.

## Validation

`delayed_consume` covers visible head stability, scalar/shared-handle reads, all
duplicate request combinations, empty failures, capacity, simultaneous writes,
latency zero/one/multiple, stall, reset, repeated advancement, and flush rejection
without mutation. Existing RID tests cover exact identity, wrap distinction,
comparators, and aligned visible/staged survivors.

`queue_method_visibility` uses immutable messages and bound const method targets
to compare each cycle in serial and 1/2/4-worker runs. It checks blocked identity,
reset replay, termination, no-progress deadlock, ownership, logs, and reports.
The full C++11 CTest suite covers transaction, Mesh, pipeline, trace, template,
and documentation examples. ThreadSanitizer is an additional check when the host
compiler/runtime supports it.
