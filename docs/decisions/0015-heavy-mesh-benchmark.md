# 0015: Controlled host computation in the Mesh benchmark

## Decision

Keep the 40-node Mesh topology, queue ownership, traffic, and guest cycle timing
fixed. Add an opt-in fake ALU evaluation to each MeshNode Work call, including
idle/blocked network cycles, to vary useful host computation per scheduled task
without changing the amount of traffic or the executor. A constructor parameter
sets the number of dependent uint64 arithmetic rounds. Zero is the default.

The pure helper consumes explicit private state and iteration count, returns a
new state, and never touches queues, globals, or input packets. Xfer commits the
pending state. Reset restores a deterministic per-node seed and clears the round
counter. Reported/checked checksums make the computed result observable; there
are no sleeps, volatile inner-loop accesses, shared synthetic counters, or
simulated queue latency changes. Synthetic evaluation does not call MarkProgress,
so permanent network blockage still triggers deadlock.

This tests host computational granularity, not the latency/throughput of a real
multi-cycle instruction. Holding mesh size fixed isolates one cause of limited
parallel scaling. Larger topology and memory-heavy workloads are separate
experiments, not conclusions implied by this arithmetic benchmark.

## Measurement and validation

A standalone Release benchmark accepts iteration/worker lists, warmup count,
batch/repetition counts, and a CSV destination. Mode zero is the required serial
baseline; mode one uses the same executor as multiple workers. Build once per
configuration and reuse threads across runs. Rotate mode order between batches.
Time only Run with a steady clock and record process CPU time separately.
Exclude reset, initialization, validation, and output from the timer.

Validate packet timings and the fixed 1560-packet, 132-cycle, 6760-hop,
19246-packet-cycle baseline after every run. Check synthetic round counts and
checksums across repetitions and worker counts. Report median batch means and
observed range rather than only a best case. Do not gate CTest on performance.

Focused tests compare every-cycle queue snapshots and per-packet timing against
ordinary Mesh, verify synthetic-state determinism and Reset, and ensure fake
work does not hide deadlock. Keep the source generator authoritative and cover
the benchmark/CSV summary with small CTest workloads. See the
[benchmark instructions](../../examples/mesh/HeavyMesh.md).
