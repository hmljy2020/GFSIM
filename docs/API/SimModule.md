# SimModule API and authoring template

Header: `include/SimModule.h`. Namespace: `gfsim`. Requires C++11.

`SimModule` is an abstract `SimObj` with optional typed ports, a non-owning parent link, owned child modules, and owned internal queues. A concrete module can have **zero or more** inputs and **zero or more** outputs. Start from [`templates/SimModule.h.in`](../../templates/SimModule.h.in), [`templates/SimModule.cpp.in`](../../templates/SimModule.cpp.in), and [`templates/SimModule.md.in`](../../templates/SimModule.md.in), then add only the ports, private state, and helper methods needed. Configure `@MODULE_NAME@` and compile the generated module implementation; its [SimState counter](SimState.md) uses the public header directly.

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
| `BindModule(T*)` | Protected template: register a non-owning module target and return the same typed pointer, including constness. |
| `AddInnerQueue(std::shared_ptr<SimQueue<T>>)` | Own an internal queue and return a pointer for child ports. |
| `Log()` / `Report()` | Access the system logger and reporter; require an attached system. |
| `Trace()` | Return the optional trace writer for module slices and flows. |

`INPUT`, `OUTPUT`, and `INNER` are visual role markers. The binding methods reject null or duplicate ports within one role. Binding the same queue once as input and once as output on one module is allowed for a self-loop. Each output may have its own payload type, for example `SimQueue<T4>* output1` and `SimQueue<T5>* output2`.

Arithmetic and enum payloads move by value; class/struct payloads use `std::shared_ptr<T>`. A class-message source allocates a new object. Later stages peek at the shared handle, compute, write it to the output, then pop the input; `packet->field` accesses the same object. See the [queue API](SimQueue.md) and [atomic transaction rules](SimPure.md).

Register root modules with `SimSystem` before build. Physical queues must be owned in a parent module's inner queue collection. Every queue must have exactly one output producer and one input consumer; no unconnected queue is allowed. The dataflow graph may contain cycles. Do not register a queue directly with the system.

See [`tests/simmodule/test_simmodule.cpp`](../../tests/simmodule/test_simmodule.cpp) for a parent with two children and distinct output types, and [decision 0002](../decisions/0002-simmodule-template.md) for the ownership contract.

After `Build()`, `SimSystem::WriteDot()` renders the module hierarchy and bound queues; see the [DOT graph API](SimGraph.md).

## Module references and ordinary methods

Initialize a typed member in the constructor with `target_(BindModule(target))`,
as in [MethodCaller](../../examples/methods/MethodCaller.cpp). T must derive from
SimModule. A `const Server*` target stays const; a `Server*` stays mutable. For an
optional reference use `target ? BindModule(target) : nullptr`. No marker macro
is required. References do not enter Objects, Submodules, queue collections, or
object capacity and never acquire ownership of the target.

Bindings may be declared during construction or Build. Null or duplicate targets
on one caller throw `std::invalid_argument` (including rebinding the same object
through a const pointer). After topology freeze, binding throws `std::logic_error`,
including after Reset. Once all modules have built, the system collects the
complete module set and rejects any target outside it with `std::logic_error`.
Claiming a GetSim context without registration does not establish membership.
Unregistered targets and targets registered only in another system are invalid.
Cross-hierarchy, cross-root, self, and mutually cyclic references in one system
are allowed. Explicit Build-time wiring supports cycles; references are never
followed by recursive Build or lifecycle traversal.

The model guarantees pointer lifetime and keeps its bound member pointer fixed
after Build. Validation checks registration, not C++ lifetime. The framework does
not intercept pointer assignments or method calls.

Methods are ordinary explicit synchronous C++ calls on the **caller's thread**.
The target's Work does not automatically invoke them. During Work, methods may
query current state, stage SimState writes, and perform legal queue operations.
Only the owner commits a state during Xfer. Different workers may write different
instances; the model must prevent concurrent writes to one instance, including
different members of a struct value. Mutable diagnostics in methods need the
same discipline. Cross-module state interaction belongs in Work, never Xfer.

Both phase barriers and existing task assignment remain unchanged. Binding adds
no dispatch table, thread switch, task, lock, arbitration, conflict detector,
runtime phase check, or automatic method tracing. It does not replace queue
BindInput/BindOutput or relax queue readiness and parallel access rules.

DOT adds one dashed caller-to-target edge labeled `method` for each binding.
It records neither reference-port names nor individual method names or runtime
call counts. See [Graph API](SimGraph.md) and [decision 0017](../decisions/0017-module-method-bindings.md).

This executable example uses the supplied two-state model. Link
`method_example_model` (which links gfsim) and include `examples/methods`:

```cpp
#include "MethodBench.h"

int main()
{
    gfsim::SimSystem sim;
    auto bench = std::make_shared<method_example::MethodBench>(&sim);
    sim.AddModule(bench);
    sim.SetWorkerCount(4);
    sim.SetParallelChildren(bench.get());
    sim.SetDeadlockThreshold(2);
    sim.Build();
    sim.Run(8);
    GFSIM_ASSERT(sim.NeedTerminate() && sim.Cycles() == 7);
    GFSIM_ASSERT(bench->Target().Read(0) == 16 && bench->Target().Read(1) == 26);
}
```

The [example guide](../../examples/methods/MethodBench.md) gives per-cycle values,
reporting, reset behavior, worker configuration, and DOT rendering commands.

## Embedded SimState fields

The module template embeds a `SimState<std::uint64_t>` counter initialized to
zero, with inline `Read()` and `Write()` accessors. It stages the increment in
Work, logs the computed new count, and reports the committed count as `Work cycles`
after Xfer. Each additional logical field is another SimState instance; no
concrete State class needs to be generated.

Call each state's Build, Xfer, Reset, and ReportStat explicitly from its owner;
skip its empty Work. Embedded states do not enter Objects, Submodules, queue
collections, system roots, DOT, or worker planning. Exclude them from object
capacity. They do not acquire Parent/GetSim context and are not automatically
frozen or checked by system traversal. The owner maintains their fixed shape
and lifetime. Multiple states or fixed arrays are allowed with separately named
elements and lifecycle calls. See [State API](SimState.md) for field access and
parallel writer responsibilities.

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
pending state, and commit pending state in Xfer. Embedded state Reset restores
initial current values and clears written flags; pending storage need not be
cleared. Current fields may be read across modules during Work, and separate
workers may write separate SimState instances using ordinary non-owning
pointers. The model must prevent concurrent writes to the same instance, including members of a struct value. Do not
read another module's mutable state concurrently outside this contract. Packet
transfer remains through queues.

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

For an example that varies host Work cost without changing simulated queue
latency, see [heavy Mesh](../../examples/mesh/HeavyMesh.md). Its pure arithmetic
helper computes pending private state during Work and commits it in Xfer;
checksums and round counters are read only while workers are idle. Artificial
benchmark computation alone does not count as progress toward packet delivery.
