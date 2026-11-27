# 0014: Queue task affinity and independent Xfer commits

## Decision

Separate physical ownership from advancement assignment. Keep AddInnerQueue,
Objects(), typed port bindings, Reset traversal, DOT, and trace paths unchanged.
After Build validates the topology, map each module in a selected child subtree
to that subtree's stable task ID. Assign each queue to its producer task when
that task runs on a worker; otherwise use its consumer task when available.
With neither endpoint on a worker, retain owner-based advancement. Use the
physical owner/queue registration traversal to order assignments deterministically.

Every Xfer worker task advances its assigned queues and calls its subtree root's
Xfer. The owner's AdvanceInnerQueues helper skips delegated queues. This avoids
double advancement when physical owner and endpoint are in different subtrees
or different roots. The plan and skip set are frozen before workers start and
reused across Reset. Partial worker construction and failed queue publication
use the existing cleanup and failed-Step recovery mechanisms.

The system still has two phases: all module Work finishes before any boundary
publication, and all Xfer tasks finish before the next Work. The legacy queue
method named Work performs boundary publication in the system Xfer phase;
module Work and queue advancement must never execute concurrently. Independent
module commits and queue advancement within Xfer need no additional barrier.
Parent-child traversal and testbench drive/check ordering remain intact.

Module Work makes readiness decisions using its input side and producer-side
Full(), whose occupancy ignores same-cycle consumer Pops. Module Xfer commits
private state and calls lifecycle helpers; it must not inspect queue capacity,
queue contents, or another module's concurrently committed state. Queue queries
for next-cycle decisions belong in the next Work. Physical ownership does not
establish an Xfer read ordering. This replaces the earlier permitted dependency
on a parent advancing queues before child Xfer sampled their state.

## Mesh migration

Remove SampleCapacity and the five cached availability bits from the generated
router and generator templates. Each output checks its actual Full() during
Work. The queue already provides the admission snapshot that decision 0010
previously implemented in the model. No capacity cache is sampled during Reset.

All 214 physical Mesh queues remain under MeshBench. The 134 directed links and
40 delivery queues follow their producer nodes; the 40 injection queues follow
their consumer nodes. No topology or payload representation changes are needed.

## Validation

Retain every-cycle serial/1/2/4-worker comparison of queue contents, delays,
packet timings, reset runs, and reports. Check full outputs populated after
Build without an intervening module Xfer, retaining blocked packet identity
until direct Work admission succeeds.

Instrument ring queues to verify producer affinity, consumer fallback for a
main-thread producer, nested endpoint lookup, and owner fallback for two main
thread endpoints. Exercise parent ownership, ownership outside the split root,
and ownership on the opposite worker. Controlled rendezvous proves queue
advancement overlaps across workers; counters verify exactly one advancement
per cycle, latency, and reset. Queue-advance failure must wait for other workers,
discard pending termination, and require successful Reset before continuing.
Run the C++11 CTest suite, generated-source check, documentation compilation,
and ThreadSanitizer parallel/Mesh tests.

Validation completed with all 17 CTest tests, all six C++ API documentation
snippets, template/codegen checks, and GCC 14.4 ThreadSanitizer parallel and
Mesh tests passing. The three delegation topologies were rendered with Graphviz;
physical queue containment remains independent of the checked advancement thread.

A local Release measurement on Kunpeng-920 with GCC 10.3.1, CPUs 0-7 on one NUMA
node, logging/tracing off, and unchanged assertions used 9 batches of 10 timed
runs per mode after warmup. Reset, Build, and destruction were outside the Run
timer. Median batch means were 60.54 ms serial, 40.78 ms with two workers (1.48x),
and 28.64 ms with four workers (2.11x). A rerun of the prior four-worker executable
with parent-thread queue advancement measured 46.86 ms. These numbers describe
this Mesh workload and host, not a general scaling guarantee.
