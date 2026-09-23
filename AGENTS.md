# GFSIM agent instructions

These rules apply to every agent changing this repository. Treat them as required project conventions.

## Platform boundary

- Keep GFSIM self-contained. Do not add source, build, documentation, or naming dependencies on other simulator repositories.
- Use C++11 for library code and tests. Keep the build working through CMake and CTest.
- Keep each design decision in `docs/decisions/` and update API pages in `docs/API/` when public interfaces change.

## Required SimModule pattern

- Start each new module from `templates/SimModule.h.in`, `templates/SimModule.cpp.in`, and `templates/SimModule.md.in`. Give one concrete module one header, one implementation file, and a nearby module document.
- Derive concrete modules from `gfsim::SimModule`. A module may have zero or more `INPUT` queues and zero or more `OUTPUT` queues. Declare only ports that exist; never add dummy ports to satisfy a shape.
- Declare each boundary port as a typed, non-owning `SimQueue<T>*`. Bind inputs with `BindInput()` and outputs with `BindOutput()` in the constructor. Keep the `INPUT` and `OUTPUT` markers on declarations.
- For `SimQueue<T>`, arithmetic and enum `T` values (such as `int` and `bool`) travel directly. Class/struct `T` values travel as `std::shared_ptr<T>`; do not wrap `T` in `std::shared_ptr` at the template argument. Allocate each new class/struct message once at its producing source with `std::make_shared<T>()`. At later stages, peek with `Front()`, write the shared handle to the next queue, then `Pop()` the input after successful output admission. Use `packet->field`; never deep-copy a large payload just for handoff. Mutate a forwarded object only after its output write succeeds.
- Give every `SimObj` a stable, nonempty name. Implement `Build()` on every concrete object. In each parent's `Build()`, first call `SetObjectCapacity(n)` with the exact number of direct child modules plus owned queues, then create and wire them, then call `BuildInnerQueues()` and `BuildChildren()`. The `Objects()` array is frozen at the end of system build; never add objects or change port bindings afterwards.
- Every physical `SimQueue` must be owned by a parent module through `AddInnerQueue()`. Do not register a queue as a root object in `SimSystem`, and do not keep a queue physically outside a parent. Each queue must have exactly one `BindOutput()` producer and one `BindInput()` consumer at build validation. No unconnected queue is allowed. Cyclic dataflow, including a self-loop, is allowed.
- Child modules use `Parent()` and `GetSim()` when they need their context. Do not register a child or inner queue separately with `SimSystem`. The system has no topology parameters; all object counts and connections are fixed during `Build()`.
- Queue capacity must be in the range 1..512. Treat a capacity above 512 as a model error.
- Keep module state private and define local helper methods for distinct behavior. Reset all transient state in `Reset()`.
- `SimModule` is abstract. Implement the same five public methods as `SimObj`: `Build()`, `Work()`, `Xfer()`, `Reset()`, and `ReportStat()`. Do not introduce alternate lifecycle names. For a parent, call `WorkChildren()` after its own work, `AdvanceInnerQueues()` then `XferChildren()` in `Xfer()`, `ResetInnerQueues()` then `ResetChildren()` in `Reset()`, and `ReportChildren()` after its own report.
- In `Work()`, check all required inputs, output capacity, and other blocking conditions before calling `Read()` or `Pop()`. Transfer data only through queues. Keep `Xfer()` for phase-boundary bookkeeping and inner queue advancement.
- Mark side-effect-free combinational helpers with the empty `PURE` macro. A module may have many such methods. Prefer `PURE static` methods that receive every input explicitly, read no module/global state, do not touch queues, and do not mutate input messages. Compute from `Peek()` const values only after checking readiness; use `Front()` when the shared handle itself must be forwarded.
- Treat a queue-to-queue operation as one model-level atomic transaction: all required inputs are nonempty and all target outputs have space before computing. For multiple writes to one output, reserve enough slots with `Full(requiredWrites - 1)`. Peek inputs, run `PURE` helpers, write all outputs, and only then `Pop()` the consumed inputs. If any precondition fails, do not write or pop; `Front()`/`Peek()` are the only allowed input access for a speculative decision. Do not use `Read()` before output admission. A source with no input or sink with no output applies the same readiness/commit discipline to the ports it has.
- Implement `ReportStat()` and report meaningful counters with units and denominator context through `Report()`. Use `Log()` for meaningful state transitions, stalls, and errors with a stable module and stage name. Do not use direct `std::cout` or `std::cerr` in a module.
- Call `GetSim()->MarkProgress()` only when useful work occurs. Call `RequestTerminate(reason)` when the model's documented completion condition holds. Document these events and choose a deadlock threshold appropriate to the pipeline; test both termination and no-progress deadlock.
- Different output ports may have different payload types, such as `T4` and `T5`. No fixed port-count module belongs in `include/`.
- Add focused tests for queue flow, blocked cycles, reset, child ownership, and log/report output relevant to the new module. Extend the CTest build.
- Test a blocked output and show that input pointer identity and payload remain intact until the output write succeeds and the input is popped.
- For RID-bearing packets, declare a `RID rid` field or set a queue RID extractor. Use `FlushIf(rid)` only for exact identity; implement younger-than or thread/block-sensitive recovery with the comparator/predicate overload. Verify both visible and staged entries and their delay counters after a flush. Never infer age from a wrapped RID value alone.

## Memory, testbench, and assertions

- Use `SimSystem::GetMemory()` for guest memory. Map ranges explicitly or load a validated ELF image before simulation. Define image relocation and runtime setup in the owning application if needed.
- Derive concrete stimulus/checking components from `SimTestbench`. Keep ports optional, implement `DriveStimuli()`, `CheckOutputs()`, `Done()`, and `ResetBench()`, and record only stimuli actually sent and outputs actually verified.
- Use `GFSIM_ASSERT(condition) << context` for internal invariants and output mismatches. Use ordinary exceptions for recoverable input errors. Do not use the C assert macro for required runtime checks.
- Test mapped and unmapped memory, signed loads, ELF file bytes and zero-fill, malformed ELF rollback, expected testbench completion, and a deliberate output mismatch when these facilities change.

## Trace and swimlane

- Own all physical queues through `AddInnerQueue()` and call `BuildInnerQueues()` so tracing sees them. Give queues and modules stable, descriptive names.
- Provide `SetTraceFormatter()` for pointer or non-streamable queue payloads. Formatters must preserve the meaningful payload fields and have no side effects.
- Use `Trace()` to add module durations or dependency flows when the module has meaningful timing events. Keep event names and lane paths stable.
- Keep default every-cycle queue snapshots complete. Choose a larger sampling interval explicitly for long runs, and document it. Close a trace with `DisableTrace()` before resetting the system or destroying the output stream.
- Test both staged and visible queue contents, cycle numbers, JSON escaping, and the expected swimlane events when trace behavior changes.

## Topology graph

- Keep parent/child relationships in `AddSubmodule()`, queue ownership in `AddInnerQueue()`, and typed port bindings in `BindInput()`/`BindOutput()` so `SimSystem::WriteDot()` can discover the fixed topology.
- Give modules and queues descriptive names. Generated DOT uses nested `subgraph cluster_*` blocks for modules, cylinder-shaped queue nodes, and directed input/output edges. Review the DOT and render it with Graphviz when topology changes.

## Review gate

Before finishing a change, run a C++11 build and `ctest --output-on-failure`. Check that documentation examples compile and that repository files contain no references to other simulator repositories. Do not claim completion with failing tests.
