# MethodTarget

## Purpose and ports

Own two independent integer fields, initialized to 10 and 20. This module has no
queue ports, children, or owned queues; Build declares object capacity zero.
The header, implementation, and lifecycle layout follow the SimModule templates.

## State and cycle behavior

`first_` and `second_` are private embedded `SimState<int>` instances named
`target.first` and `target.second`. Build/Xfer/Reset/ReportStat explicitly visit
both; neither is a topology object. `Read(field)` returns current by value.
`Write(field, value)` stages that field and returns the executing thread ID for
the example's checks. Both validate the field index with GFSIM_ASSERT.

Work only records its own thread and increments a Work counter. It never invokes
Write automatically and does not mark progress. Xfer commits both states on the
target's scheduled thread; callers never access the target during Xfer. Reset
restores constructor values and clears all counters and thread IDs.

Each field has one writer, including its corresponding diagnostic `writes_`
array element. Read is const and does not change counters. Diagnostics are
inspected after workers finish. Distinct workers never write the same field;
there are no locks or phase guards. Read/Write are stateful methods, not PURE.
No queue transactions or blocking conditions apply to this target.

## Log, report, trace, and tests

The callers log actual writes. Target reports Work calls in cycles since reset,
per-field writes in method calls since reset, and committed values in units.
It neither marks progress nor requests termination. No queue snapshots or timing
slices are needed for these synchronous state methods; binding produces topology
edges, not runtime method traces.

`method_interaction` checks thread identity, no implicit method calls, delayed
visibility, holding, reset, counters, and serial/parallel equivalence. See the
[bench](MethodBench.md) for completion and deadlock behavior.
