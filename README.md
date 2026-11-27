# GFSIM

Graph Flow Simulator is a C++11 platform for cycle-based, queue-wired models.

## Platform pieces

- `SimSystem` builds named objects, schedules Work and Xfer phases, checks termination and deadlock, and owns logging and reporting.
- `SimQueue<T>` carries small arithmetic/enum values directly and class/struct messages as `std::shared_ptr<T>`, with staged writes, visible reads, capacity up to 512, latency, and stall control.
- `RID` and `SimQueue::FlushIf()` remove selected packets from both queue phases while preserving staged delay alignment.
- `SimModule` owns nested modules and internal queues while exposing optional typed input and output ports.
- `PURE` marks stateless combinational helpers; modules peek, compute, write outputs, then consume inputs only when the whole queue transaction can fire.
- `Reporter` formats counters; `Logger` emits cycle-aware diagnostic lines.
- `SimMemory` maps guest addresses and loads ELF images; `SimTestbench` drives and checks models; `GFSIM_ASSERT` reports invariant failures.
- `SimTrace` records every registered queue's contents and depth by cycle, with optional module swimlane slices and flows.
- `SimSystem::SaveDot()` writes nested module and queue topology for Graphviz SVG/PDF rendering.

The [queue API](docs/API/SimQueue.md) defines the payload policy, and the [DOT graph API](docs/API/SimGraph.md) shows topology export. Every queue is owned by a parent module and connects exactly one output to one input.

Start a concrete module from the code and document files in `templates/`. The required rules are in [AGENTS.md](AGENTS.md); interface details are in [`docs/API/`](docs/API/).

## Build and test

```sh
cmake -S . -B build -DCMAKE_CXX_STANDARD=11 -DCMAKE_CXX_STANDARD_REQUIRED=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The library target is `gfsim`. Applications link to it and include headers from `include/`.

For a runnable request/reply example, see [the counter exchange](examples/counter/CounterSystem.md).
Build and run `counter_example` to watch a client send two additions and verify the counter's replies cycle by cycle.

The [single-threaded mesh example](examples/mesh/MeshBench.md) routes 1,560 packets
between 40 nodes on an 8×5 grid. Run `./build/mesh_example` for the cycle and hop
counts, or `./build/mesh_example --timings build/mesh-timings.tsv` for the per-packet baseline.

To execute independent child subtrees on persistent workers, call
`system.SetWorkerCount(4)` and `system.SetParallelChildren(parent)` before
`Build()`. Work and Xfer each end with a barrier; the selected parent's own
logic and queues stay on the main thread. See the [parallel execution
contract](docs/API/SimSys.md#parallel-child-subtrees) and
[queue access restrictions](docs/API/SimQueue.md#parallel-access-contract).
CTest compares the Mesh's serial baseline against 1, 2, and 4 workers every cycle.
