# 0006: Queue snapshots and swimlane events

Status: provisional

## Context

Cycle debugging needs the exact contents of every queue, including entries still staged for a later cycle. Timeline analysis also needs queue occupancy and module activity on named tracks. Payload types are open-ended, so a generic queue cannot assume every value is directly printable.

## Decision

- `SimSystem` maintains a registry of parent-owned inner queues. `SimModule::BuildInnerQueues()` registers them by hierarchical path. Every sampled cycle is captured after the complete Work/Xfer step.
- `SimTrace` streams a Perfetto-compatible JSON trace. Each queue emits a full-content instant event and a depth counter event on its own named track. Module code may add duration slices and dependency flows.
- Streamable message objects have a default formatter. Non-streamable message objects require an explicit formatter that receives `const T&`. A missing formatter raises an error instead of silently dropping content.
- Sampling defaults to every cycle; callers may choose a larger interval to reduce output volume. The writer does not buffer earlier cycles.

## Consequences

Trace output can become large when queues are deep or simulations are long. A queue must be owned through the system or a parent module, or explicitly registered, to appear in the trace. The output stream must outlive the enabled trace. Timestamps express cycles, not physical time units; applications can convert them using their clock model.
