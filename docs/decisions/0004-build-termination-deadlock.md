# 0004: Build, termination, and deadlock bookkeeping

Status: provisional

## Context

A simulation graph needs a named build phase before cycles execute. The system also needs an explicit completion signal and a bounded way to detect a graph that stops making useful progress.

## Decision

- Every `SimObj` has a stable, nonempty `Name()` and a `Build()` method. `SimSystem::Build()` calls each registered root's `Build()` once, then resets the built graph. Concrete parent modules construct their children and internal queues in `Build()`, then call `BuildInnerQueues()` and `BuildChildren()`.
- `Step()` requires a successful build. The graph cannot add new root objects after build. `Reset()` clears run state but preserves the built graph.
- A module calls `MarkProgress()` for an accepted or completed operation during `Step()`. After each step the system resets its stalled-cycle count on progress or increments it otherwise. If the count reaches a nonzero configured threshold, it enters deadlocked state, raises `DeadlockError`, and emits an error log. Only `Reset()` clears that state.
- A module calls `RequestTerminate(reason)` when the modeled completion condition is satisfied. Termination is checked after the current Work/Xfer step. It takes precedence over deadlock detection in that step.
- `Run(maxSteps)` bounds execution even when deadlock checking is disabled.

## Consequences

The platform cannot infer useful progress from arbitrary module state. A module that does useful work without calling `MarkProgress()` may trigger a false deadlock. The threshold defaults to zero, which disables deadlock checking until a model chooses a suitable value. Model authors must define progress and completion events in the module document and test both normal termination and no-progress behavior. A failed build cannot be retried on the same system because it may have partially constructed the graph.

The [three-stage pipeline test](../../tests/pipeline/test_three_stage_pipeline.cpp) exercises build order, one queue handoff per stage, termination, reset, and deadlock detection.
