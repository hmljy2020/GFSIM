# MethodCaller

## Purpose, references, and objects

Two instances, `first_caller` and `second_caller`, query and update different
fields on MethodTarget. The constructor retains a typed non-owning
`MethodTarget* const target_` initialized with `BindModule(target)`. The target
outlives both callers through their common parent. There are no queue ports,
children, or owned queues; object capacity is zero. The module follows the
SimModule header, implementation, and document templates.

## State and cycle behavior

Work records its thread and invokes CallTarget before WorkChildren. During cycles
1 through 6, CallTarget records the old current value. The first caller writes
current + 1 every cycle; the second writes current + 2 only on odd cycles.
`PURE static NextValue(current, field)` uses only explicit scalar arguments.
Disabled callers and callers past cycle 6 perform no method calls. The second
caller's even cycles intentionally hold state; there are no queue transactions
or output-admission stalls in this example.

Write returns its thread ID, which must equal the caller's thread. A Read
immediately after Write must still see old current. Writes only stage pending
values; target Xfer commits them after all Work completes. Each caller is the
sole writer of its field and the associated target diagnostic counter. No
cross-module access occurs in caller Xfer. Reset clears observations, write
counts, and thread diagnostics. Xfer/Reset/ReportStat retain the template helpers.

## Log, report, trace, and tests

Every successful Write calls MarkProgress and emits a `method` debug event with
the staged value. Even second-field cycles emit a `hold` event with current.
Reports count writes in method calls since reset and observations in active
cycles since reset. The callers never request termination themselves.
There are no physical queues or asynchronous timing events to trace.

`method_interaction` checks same-cycle reads, next-cycle visibility, field holds,
method execution on caller threads, reset replay, logs and counters. The bench
selects worker execution and owns completion and deadlock checks.
