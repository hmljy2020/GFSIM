# Nested module, queue, and method DOT graph

`SimSystem::WriteDot(std::ostream&)` and `SimSystem::SaveDot(path)` export the built topology as Graphviz DOT. Call them after `Build()`.

- Every `SimModule` becomes a labeled `subgraph cluster_*`. Child modules are nested inside the parent cluster.
- Every physical `SimQueue<T>` becomes a cylinder-shaped node inside its owning parent cluster. No root queue is allowed.
- `BindOutput()` produces a module-to-queue edge; `BindInput()` produces a queue-to-module edge. Every queue has one of each. Rings are rendered as directed cycles.
- `BindModule(target)` produces a caller-to-target module edge with `style=dashed, label="method"`. Const targets have the same edge style. References add no nodes or ownership clusters.
- Names are DOT escaped. Node IDs are assigned from the built registration and child order.

Method edges follow module ownership traversal order and each caller's binding
order. Cross-hierarchy, cross-root, self, and cyclic references are supported;
they do not affect queue edges or cluster nesting. Models with no module bindings
produce the same graph as before. One edge represents one bound target, not a
named reference port, a particular method, or a runtime invocation count.

Build validates target membership before export. Reference cycles do not drive
graph traversal or worker placement. See the [Module API](SimModule.md#module-references-and-ordinary-methods)
for lifetime and concurrent method-call responsibilities.

The [graph test](../../tests/graph/test_simgraph.cpp) builds a complete parent/child pipeline and calls both export methods.

Save a diagram and render it with Graphviz when the `dot` CLI is available:

```sh
dot -Tsvg topology.dot -o topology.svg
dot -Tpdf topology.dot -o topology.pdf
```

The GFSIM library does not require Graphviz. The [topology contract test](../../tests/graph/test_topology_contract.cpp) also checks fixed object counts, one-to-one queue ports, legal cycles, and the 512-entry capacity limit.

The [method example](../../examples/methods/MethodBench.md) exports three method
edges. `bind_module` verifies direction, style, count, stable order, self/cyclic
references, and coexistence with queue edges. When Graphviz is available at CMake
configuration, `method_graph_render` and `module_references_render` render both
graphs to SVG in the build directory. Set `GFSIM_DOT` to the `dot` executable if
it is not on PATH.
