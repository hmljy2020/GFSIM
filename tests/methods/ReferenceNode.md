# ReferenceNode test fixture

## Purpose, ports, and children

This module follows the SimModule templates and exposes BindModule publicly only
for API contract tests. An optional const constructor target is bound when
non-null. Constructor-configured children are added in Build. Optional `feedback`
is an owned scalar SimQueue<int> bound to both INPUT input_ and OUTPUT output_;
it stays empty and tests graph coexistence with a legal queue self-loop.
Capacity is the number of direct children plus one when feedback exists.
Reference targets add no capacity or ownership. Build ends with BuildInnerQueues
and BuildChildren.

## State and lifecycle

Work counts calls and traverses children. Xfer advances inner queues and traverses
children; Reset clears the counter, resets queues, then children. ReportStat
reports Work calls in cycles since reset and traverses children. No PURE helper,
queue transaction, payload mutation, progress event, termination, or meaningful
module timing event is needed for this topology-only fixture. It has no output
blocking behavior. Empty scalar queue snapshots use the default formatter.

## Tests

`bind_module` checks null/duplicate/frozen bindings, typed/const returns, optional
absence, membership rejection, cross-root/hierarchy relationships, self/cyclic
references, capacity and task exclusion, and stable DOT with existing queue
edges. It steps the cyclic graph to verify ownership traversal does not follow
reference cycles. No runtime reference rewiring is allowed after Build.
