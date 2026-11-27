# ProbeModule

Adapted from the three SimModule templates. This zero-port test module registers
its configured children during Build, with an exact object capacity equal to
child count plus optional RingPacket queue count. Optional physical queues are
registered through AddInnerQueue; the probe still has no boundary ports. Hooks before and after child traversal let the
fixture record lifecycle ordering, rendezvous independent workers, request
termination, and inject ordinary/assertion/reset failures. Hooks are configured
only while idle. Work and Xfer counters are private and reset to zero; hooks
and fixed child configuration survive Reset.

The Work counter represents completed entry into a probe evaluation, not model
progress. Only fixtures that model a completed probe operation call
MarkProgress from a hook. Termination hooks explicitly test phase/task priority.
No-input idle probes deliberately deadlock at the fixture's threshold.

Work emits one `Name().work` debug record with the cumulative operation count,
a one-cycle `work` slice with escaped detail, and a paired dependency flow on
its path. Reports count Work and Xfer calls since reset. Child helpers execute
once regardless of the hook's local decision; an injected exception instead
aborts the phase as documented by the system API.

`test_parallel.cpp` checks exact counts, nested ownership and traversal,
concurrent phases, reset/thread reuse, complete logs, reports, and recovery.
`check_trace.py` parses the trace JSON, lane count, escaped detail, and flow IDs.

Delegation fixtures use optional queues to separate physical ownership from the
producer/consumer task, including owners outside the selected root and owners
on a different worker. Owner reset clears them regardless of advancement task.
