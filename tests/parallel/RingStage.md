# RingStage

Adapted from the three SimModule templates. One INPUT `SimQueue<RingPacket>*`
and one OUTPUT of the same type bind in the constructor. Build declares one
object and owns the output queue through AddInnerQueue by default, then builds
queues and children. With ownOutput=false it declares zero objects and the
fixture supplies physical ownership through a ProbeModule. Each queue has capacity 1, latency 2, and a formatter `id:hops`.
The two-stage fixture connects a.to_b to b's input and b.to_a to a's input;
each worker advances its assigned producer queue. Physical ownership may be
elsewhere in the delegation tests. No cross-owner reads occur in Xfer.

The fixture is the initial packet source, allocating each RingPacket once with
make_shared while simulation is idle. A stage peeks the shared handle and checks
output admission. If blocked it retains input identity and content, increments
a blocked-cycle counter, and emits `Name().blocked`. Otherwise `NextHop` is a
PURE static increment of the explicit hop count; the stage writes the handle,
pops the input, then updates the exclusively owned payload and marks progress.
It reports and logs each accepted forwarding operation. ID 99 is a discard
sentinel consumed without an output; a packet at hop 4 is consumed and requests
`ring complete`. Both successful sinks mark progress. No speculative read or
pop occurs before readiness. Local blocking returns from Transfer so Work still
calls WorkChildren.

Reset clears forwarding/blocked counters and resets owned queues and children.
Reports label forwarded packets and blocked cycles since reset. Queue snapshots
remain every-cycle, covering both visible and staged packets and delay values.
The fixture's deadlock threshold 8 exceeds the one-cycle quiet gap caused by
queue latency 2; a reset ring without stimuli intentionally deadlocks.

Tests cover cross-thread identity, full-at-cycle-start admission after a
concurrent pop, exactly one queue advancement per cycle, staged delays, cyclic
flow, termination at cycle 10, independent owners, reset/replay, logs, reports,
trace JSON, and DOT ownership/port edges.

Delegation tests instrument queue advancement to check actual thread identity,
parallel overlap, exactly-once publication, reset reuse, and propagation of
queue-advance exceptions. WorkThread records the stage executor for comparison.
