# 0013: Parallel execution of fixed child subtrees

Queue execution assignment and the Xfer access contract below are updated by
[decision 0014](0014-queue-task-affinity.md). Ownership still controls lifetime
and topology, but no longer fixes the advancement thread.

## Decision

Provide `SetWorkerCount(count)` and `SetParallelChildren(parent)` before Build.
A single selected parent's direct children form stable tasks numbered 1..N;
main-thread code is task 0. Assign child i to worker i modulo the effective
worker count, capped at N. Zero children create no worker. Do not change queue
ownership, port binding, module lifetime, or sequential traversal within tasks.
Default systems remain serial. A configured split uses identical semantics at
1, 2, or 4 workers.

`SimExecutionPlan` stores the split, task roots/IDs, and ordered worker lists.
`SimExecutor` owns persistent C++11 threads, one scheduling mutex, ready/done
condition variables, a generation number, phase, remaining-worker count, stop
flag, and exception pointer. Publish one phase, wake workers with a predicate,
execute model code outside the scheduler lock, register completion under the
lock, then return only after all workers finish. Main-thread code explicitly
publishes the next phase. Work of all roots precedes Xfer of all roots.

The split parent's traversal helpers are dispatch barriers. Its own work and
queue advancement stay on the main thread; queues below it advance with their
owners. Thread creation follows validation and initial Reset. Reset reuses
workers. Partial construction joins every started thread. Destruction joins
before releasing owned objects and auxiliary services.

Use relaxed atomic progress aggregation. Phase synchronization, not the atomic,
provides data visibility. Protect pending termination selection with a mutex
and thread-local task/phase context. Publish termination after Xfer and capture;
Work requests precede Xfer requests, then lower task IDs win, then first request
within that phase/task wins. Requests outside Step are immediate. Same-cycle
model code sees only published termination state.

Capture ordinary worker exceptions (including AssertionError), skip the failed
worker's remaining tasks, wait for all other workers, then rethrow the first
recorded exception. Do not enter Xfer after failed Work. Discard pending
termination and require successful Reset after a failed parallel Step, including
main-thread failures. State is not transactionally rolled back. Keep serial
exception behavior unchanged.

Serialize complete logger records and active trace events with mutexes; allow
same-cycle output order to vary. Capture, reports, configuration, and resets
remain idle-thread operations. Queue and guest-memory access restrictions are
documented rather than enforced with new queue locks or API wrappers. See the
[system](../API/SimSys.md), [module](../API/SimModule.md), and
[queue](../API/SimQueue.md#parallel-access-contract) contracts.

## Consequences

The fixed hierarchy supplies a simple deterministic assignment and preserves
parent pre/post logic, including testbench stimulus/check ordering. It cannot
load-balance within a subtree. Serial model code that reads sibling state,
flushes a concurrently used queue, or inspects sibling-owned queues in Xfer
must be adapted before enabling a split. This is an opt-in execution mechanism,
not a claim that arbitrary existing module code is thread-safe.

Templates retain the five lifecycle methods, optional typed ports, ordinary
function calls, explicit PURE arguments, current/pending state, and unconditional
child traversal despite local blocking. No external generator implementation is
required for these authoring conventions.

## Validation

`parallel_subtree` covers configuration/ownership, empty/single/excess workers,
nested splits and multiple roots, actual Work and Xfer overlap via bounded
rendezvous, phase order, stable thread assignment across reset, deterministic
termination, exception recovery, partial thread creation cleanup, and independent
worker-owned ring queues with blocked pointer identity and two-cycle latency.
`parallel_trace_json` parses concurrent event records and queue snapshots.
`mesh_example` compares all queue fields and per-packet timing every cycle,
reports, reset reruns, backpressure, checker failures, and deadlock for serial
and 1/2/4 workers. The baseline remains 1560 packets, 132 cycles, 6760 hops, and
19246 total packet-latency cycles.

The implementation was validated with the C++11 Debug build (17 CTest tests)
and all six C++ API documentation snippets (standalone snippets also run).
The generated module template compiles in `template_smoke`; Mesh generation
consistency is checked by `mesh_codegen`.

ThreadSanitizer validation uses a separate build with GCC 14.4 and
`-fsanitize=thread -fno-omit-frame-pointer -g` plus
`-fsanitize=thread` at link time. The parallel, trace-JSON, and Mesh tests pass
without race reports. The system GCC 10.3 could not link its sanitizer runtime
(`libtsan.so.0.0.0` missing); the installed GCC 14 toolchain completed this
validation, so no sanitizer check remains blocked. Reproduce with a compiler
that provides its matching sanitizer runtime:

```sh
cmake -S . -B /tmp/gfsim-tsan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=thread -fno-omit-frame-pointer -g' \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
cmake --build /tmp/gfsim-tsan --target parallel_subtree_test mesh_example_test
ctest --test-dir /tmp/gfsim-tsan \
  -R '^(parallel_subtree|parallel_trace_json|mesh_example)$' --output-on-failure
```

The ring and Mesh DOT exports were reviewed and rendered to SVG with Graphviz
14.1.2. The ring has 3 module clusters, 5 object nodes, and 4 directed port
edges; Mesh has 41 clusters, 255 nodes, and 428 port edges. Ring queues appear
inside their respective worker-owned module clusters. Repository boundary and
whitespace checks pass without introducing dependencies on another simulator.
