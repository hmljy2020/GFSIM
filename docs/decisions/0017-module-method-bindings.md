# 0017: Module reference binding and synchronous methods

## Decision

Add protected `template <typename T> T* SimModule::BindModule(T* target)`.
T must derive from SimModule; preserve the concrete pointer type and constness.
Store non-owning `const SimModule*` targets in binding order. Constructor and
Build-time bindings are allowed; null and duplicate targets on the same caller
throw `std::invalid_argument`. A const/non-const spelling of the same target is
still a duplicate. Optional references use `target ? BindModule(target) : nullptr`.
After topology freeze, all binding attempts throw `std::logic_error`.

After all root modules have built, collect the complete system module set using
ownership traversal. Reject references outside that set with `std::logic_error`,
even if the target claims this system through GetSim. Membership, rather than
construction or root-registration order, determines validity. Cross-hierarchy,
cross-root, self, and mutually cyclic references within the system are legal.
Build-time explicit wiring can connect mutually referring objects. Never follow
reference edges for building, lifecycle traversal, ownership, or scheduling.

The model keeps each pointer alive and does not rewrite bound member pointers at
runtime. Framework validation establishes registration only; it cannot validate
C++ pointer lifetime or intercept ordinary pointer assignments.

A method is an explicit ordinary synchronous C++ call on the caller's thread.
Binding does not invoke methods when the target runs Work. During Work, methods
may read current state, stage SimState writes, and use existing legal queue
operations. Each state is committed once by its owner in Xfer. Models prevent
concurrent writes to the same instance (including different struct members) and
avoid other shared mutable bookkeeping in methods. Cross-module state access is
restricted to Work. Retain both phase barriers and the existing worker assignment.
There is no method registry, dispatcher, invocation task, thread switch, lock,
arbitration, conflict detector, runtime phase guard, or new marker macro.

A module reference does not bind a queue endpoint. All queues still need their
existing BindInput/BindOutput endpoints and obey queue access/admission rules.

## Graph and capacity

Emit one caller-to-target DOT edge with `style=dashed, label="method"` per binding,
in ownership traversal order and then caller binding order. Retain existing
hierarchy, nodes, and queue edges. Models without bindings produce unchanged DOT.
No reference-port names, method names, or runtime invocation counts are recorded.

References add no owned objects, children, queues, capacity, or worker tasks.
Only the binding vector and build validation sets add framework storage/work.
This extends [decision 0016](0016-embedded-state.md) with module-level topology;
SimState itself remains unregistered and has no reference-binding interface.
Automatic lifetime checks and reference-based thread placement remain out of scope.

## Validation

`bind_module` tests concrete/const pointer returns, optional/null/duplicate/frozen
bindings, unregistered/foreign targets, cross-root/hierarchy references, self and
cyclic edges, ownership/capacity/task exclusion, deterministic DOT, and coexistence
with queue edges. Graphviz rendering tests run when `dot` is available.

The [method example](../../examples/methods/MethodBench.md) has a two-state target,
two independent callers, and a checking testbench. `method_interaction` compares
all cycles in serial and with 1/2/4 requested workers, checks method and lifecycle
thread IDs, same-cycle old reads, next-cycle visibility, unwritten values, reset
replay, logs/reports, completion, and no-progress deadlock. Target-only stepping
checks that its Work does not call its public mutation method automatically.
Build C++11 templates/documentation examples and run full CTest; run related
ThreadSanitizer tests when the host runtime supports them.
