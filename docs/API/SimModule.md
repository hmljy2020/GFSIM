# SimModule API and authoring template

Header: `include/SimModule.h`. Namespace: `gfsim`. Requires C++11.

`SimModule` is an abstract `SimObj` with optional typed ports, a non-owning parent link, owned child modules, and owned internal queues. A concrete module can have **zero or more** inputs and **zero or more** outputs. Start from [`templates/SimModule.h.in`](../../templates/SimModule.h.in), [`templates/SimModule.cpp.in`](../../templates/SimModule.cpp.in), and [`templates/SimModule.md.in`](../../templates/SimModule.md.in), then add only the ports, private state, and helper methods needed. The code template compiles as a zero-port module after replacing `@MODULE_NAME@`.

## Unified lifecycle

Every concrete module implements the same public methods as `SimObj`:

| Method | Module responsibility |
| --- | --- |
| `Build()` | Declare `SetObjectCapacity(n)` for the exact direct object count, create and bind optional children and internal queues, then call `BuildInnerQueues()` and `BuildChildren()`. |
| `Work()` | Perform one cycle of local behavior; call `WorkChildren()` for owned children. |
| `Xfer()` | Perform phase-boundary bookkeeping; call `AdvanceInnerQueues()` then `XferChildren()`. |
| `Reset()` | Clear private state; call `ResetInnerQueues()` then `ResetChildren()`. |
| `ReportStat()` | Write counters through `Report()`; call `ReportChildren()`. |

All five methods are pure virtual in `SimModule`. The protected traversal helpers are no-ops when the module has no children or internal queues. Check every required input, output capacity, and other blocking condition before consuming an input. Call `GetSim()->MarkProgress()` only when useful work actually occurs.

Mark stateless combinational helpers with `PURE` and commit queue transfers atomically; see the [PURE transaction API](SimPure.md).

## Ownership and ports

| Interface | Contract |
| --- | --- |
| `SimModule(std::string name, SimSystem* system = nullptr)` | Give the module a stable name. Root modules can receive a non-owning system pointer. |
| `AddSubmodule(std::shared_ptr<SimModule>)` | Own a child within declared capacity; reject null, repeated ownership, ownership cycles, or a different system. |
| `Objects()` | Inspect the fixed direct array of child modules and queues after build. |
| `Parent()` / `Submodules()` | Inspect the parent and owned children. |
| `GetSim()` / `Path()` | Return the system and the module's hierarchical name. |
| `BindInput(SimQueue<T>*)` | Bind one non-owning input; omit when the module has no inputs. |
| `BindOutput(SimQueue<T>*)` | Bind one non-owning output; omit when the module has no outputs. |
| `AddInnerQueue(std::shared_ptr<SimQueue<T>>)` | Own an internal queue and return a pointer for child ports. |
| `Log()` / `Report()` | Access the system logger and reporter; require an attached system. |
| `Trace()` | Return the optional trace writer for module slices and flows. |

`INPUT`, `OUTPUT`, and `INNER` are visual role markers. The binding methods reject null or duplicate ports within one role. Binding the same queue once as input and once as output on one module is allowed for a self-loop. Each output may have its own payload type, for example `SimQueue<T4>* output1` and `SimQueue<T5>* output2`.

Arithmetic and enum payloads move by value; class/struct payloads use `std::shared_ptr<T>`. A class-message source allocates a new object. Later stages peek at the shared handle, compute, write it to the output, then pop the input; `packet->field` accesses the same object. See the [queue API](SimQueue.md) and [atomic transaction rules](SimPure.md).

Register root modules with `SimSystem` before build. Physical queues must be owned in a parent module's inner queue collection. Every queue must have exactly one output producer and one input consumer; no unconnected queue is allowed. The dataflow graph may contain cycles. Do not register a queue directly with the system.

See [`tests/simmodule/test_simmodule.cpp`](../../tests/simmodule/test_simmodule.cpp) for a parent with two children and distinct output types, and [decision 0002](../decisions/0002-simmodule-template.md) for the ownership contract.

After `Build()`, `SimSystem::WriteDot()` renders the module hierarchy and bound queues; see the [DOT graph API](SimGraph.md).

## Parallel traversal and generated models

`WorkChildren()` and `XferChildren()` are out-of-line dispatch points. At the
configured split parent they run direct child subtrees on persistent workers
and wait for completion. Everywhere else they recurse in registration order.
Call each helper exactly once in its lifecycle phase even if local work is
blocked: put early-return readiness checks in a local helper, then call
`WorkChildren()` unconditionally. Do not call child lifecycle methods directly
in addition to the traversal helpers. Keep `AdvanceInnerQueues()` before
`XferChildren()`. The advancement helper handles queues that remain local and
skips queues delegated to workers; it does not promise that owned queues have
already advanced when it returns.

Generated GFIR functions map to ordinary calls, with explicit arguments; they
do not create boundary ports. Generate only existing ports, allow each output
its own type, and bind optional non-null endpoints in the constructor. Use
`PURE static` for combinational functions. Read current state in Work, write
pending state, and commit pending state in Xfer; Reset clears both. Do not read
another module's mutable state concurrently. Queue transfer remains the
communication mechanism between modules.

A subtree owns its mutable module state. Physical queue ownership determines
lifetime, Reset, and topology; execution assignment is computed from its producer
and consumer subtrees. A worker advances its assigned queues in Xfer, independently
of other worker tasks and owner callbacks. Module Xfer must only commit private
state and call lifecycle helpers. Do not query queues or read another module's
state in Xfer. Check input readiness and output Full() during Work, where queue
admission is independent of concurrent consumer Pops. Remove capacity caches
sampled during Xfer or Reset; the queue already maintains the admission count.

Within each system phase independent worker tasks may overlap, but module Work
and queue publication never overlap. SimQueue::Work() is the legacy publication
entry point called during system Xfer, not an ordinary module Work task.
