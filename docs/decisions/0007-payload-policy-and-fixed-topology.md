# 0007: Payload policy and fixed topology graph

Status: provisional

## Context

Large simulated messages move through many queues. Copying an object at every handoff adds cost and obscures whether two stages refer to the same logical message. The built module and queue graph also needs a reviewable topology view.

## Decision

- `SimQueue<T>` stores arithmetic/enum `T` directly and class/struct `T` as `std::shared_ptr<T>`. New class messages are allocated at their producing source; downstream stages forward the shared handle after output admission and consume the input afterward. Fields are accessed through `->`. Already-wrapped pointer template arguments are rejected at compile time.
- Each `SimObj` has a build-time-sized object collection. Physical queues live only in a parent module's inner queue collection. System build rejects a queue without exactly one output producer and one input consumer, while allowing cycles.
- `SimSystem` exports DOT after build. Modules form nested Graphviz subgraphs, all queue cylinders appear within their owning parent cluster, and typed input/output bindings become directed edges. Cylinder shapes and names distinguish queues from modules.
- The DOT writer is part of the library. SVG and PDF generation use Graphviz as an optional downstream tool.

## Consequences

Small values remain inexpensive to copy; larger message objects move by pointer. A new class output object is allocated by the module that produces it; forwarding stages reuse an existing pointer. The graph reflects declared bindings and ownership. Fan-out through one queue is not allowed by the one-producer/one-consumer contract; use explicit separate queues and define any shared mutable object policy.
