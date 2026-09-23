# 0002: Standard queue-wired module template

Status: provisional

## Context

GFSIM modules need visible port roles, hierarchical ownership, and one cycle routine for nested modules. Boundary queues should have one owner while children use non-owning pointers. Modules may have zero or more input and output ports.

## Decision

Provide `SimModule`, an abstract `SimObj` base with optional typed port bindings, parent/child ownership, and inner queues. New modules start from `templates/SimModule.h.in` and `templates/SimModule.cpp.in`; each adds only the ports, child modules, helpers, and state that it needs. Output ports may use independent payload types.

`INPUT`, `OUTPUT`, and `INNER` are visual role markers. The parent owns `shared_ptr` children and physical inner queues; its `Parent()` pointer is non-owning. `SetObjectCapacity()` fixes the direct SimObj collection during `Build()`. Only root modules are registered with `SimSystem`. A module advances its own inner queues in `Xfer()` after all children have run `Work()`.

Each module implements the same `Work`, `Xfer`, `Reset`, and `ReportStat` interface as `SimObj`. The base provides explicit helpers to traverse children and advance internal queues. Each module uses its system's `Logger` and `Reporter` through `Log()` and `Report()`. Private state is reset in `Reset()` and reported in `ReportStat()`.

## Consequences

No dummy ports or fixed port count are required. The template compiles as a zero-port module and can be extended for any queue topology. Concrete parents call the child and inner queue helpers in their five lifecycle methods. Every owned queue must connect one output port to one input port; the graph may contain cycles. No child or queue may be added after build.
