# Nested module and queue DOT graph

`SimSystem::WriteDot(std::ostream&)` and `SimSystem::SaveDot(path)` export the built topology as Graphviz DOT. Call them after `Build()`.

- Every `SimModule` becomes a labeled `subgraph cluster_*`. Child modules are nested inside the parent cluster.
- Every physical `SimQueue<T>` becomes a cylinder-shaped node inside its owning parent cluster. No root queue is allowed.
- `BindOutput()` produces a module-to-queue edge; `BindInput()` produces a queue-to-module edge. Every queue has one of each. Rings are rendered as directed cycles.
- Names are DOT escaped. Node IDs are assigned from the built registration and child order.

The [graph test](../../tests/graph/test_simgraph.cpp) builds a complete parent/child pipeline and calls both export methods.

Save a diagram and render it with Graphviz when the `dot` CLI is available:

```sh
dot -Tsvg topology.dot -o topology.svg
dot -Tpdf topology.dot -o topology.pdf
```

The GFSIM library does not require Graphviz. The [topology contract test](../../tests/graph/test_topology_contract.cpp) also checks fixed object counts, one-to-one queue ports, legal cycles, and the 512-entry capacity limit.
