# QueueView

Template-derived delayed-consumption fixture. The forwarding instance binds one
`ImmutablePacket` input and output; observer instances have no ports and bind a
const QueueView method target. No children, owned queues, or embedded state:
Build declares zero objects and calls the standard helpers.

`Inspect()` reads only Empty/Size/Front/Peek and returns a shared handle to an
immutable payload. Work records the same observation before and after forwarding
or repeated method queries. Observers never access the consumption flag or the
forwarder's counters. A ready transaction writes its handle then requests one
Read (even values) or Pop (odd values), marks progress, and logs `forward`.
A full output logs `blocked` and leaves
the input intact. Xfer only traverses helpers. Reset clears observations and
counters; ReportStat gives packets, blocked cycles, and observations since reset.

QueueBench owns the queues and supplies the completion/deadlock policy.
`queue_method_visibility` checks serial order with observers before and after the
consumer, concurrent queries with 1/2/4 workers, blocked payload identity, reset,
termination, deadlock, logging, reporting, and the DOT method relationships.
