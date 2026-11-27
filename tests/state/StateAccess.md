# StateAccess

Adapted from `templates/SimModule.{h,cpp,md}.in`. This zero-port module holds a
pair of non-owning constructor pointers to its owner's uint64_t SimState
instances and a fixed choice of instance to write. It owns no queues or children and declares zero object
capacity. No queue admission, payload forwarding, or output blocking applies.

Work reads both current fields, computes an update with PURE static NextPc
(pc + count + 1) or NextCount (count + pc + 2), and stages only its assigned field.
Both helpers take every input explicitly and have no side effects. It verifies
that reads still return the old values, records that observation, and always
calls WorkChildren. Disabled writers still observe current but stage nothing.
One module writes pc and the other writes count, with separate pending values
and ordinary bool flags. Concurrent writes to one instance would be a model error.

Only the owner commits shared state. Access Xfer records its thread ID and uses
the standard queue/child helpers without reading state. Reset clears observations
and thread IDs. ReportStat reports `current snapshots (observations since reset)`
for each module, then calls ReportChildren. These observations and thread IDs
are test instrumentation, inspected by the harness only when workers are idle.

A successful field write calls MarkProgress and logs `staged field update` at
`<name>.work`. Disabled updates log at `<name>.stall`. Completion and the two-cycle
deadlock threshold belong to StateOwner. No trace queues or module timing events
are modeled. `simstate` checks old-value observations, independent worker threads,
same-thread Work/Xfer execution, reports, reset, completion, and disabled-writer
deadlock against serial execution. There are no intentionally racing tests.
