# 0010 Mesh cycle admission and arbitration

The Xfer capacity-cache workaround below is superseded by queue write admission
in decision 0012 and direct Work checks in decision 0014.

The single-threaded mesh example is a reference workload for comparing execution orders. At the time of this decision, a consumer could pop a visible queue entry immediately during Work; [decision 0019](0019-delayed-queue-consumption.md) now defers deletion until Xfer. A producer consulting live capacity later in the same phase could then admit a packet one cycle earlier.

Each mesh output samples its available space after the parent advances the queues and after reset. The router uses this sample throughout the next Work phase. Each output has a single writer and can grant at most one packet per cycle, so one availability bit suffices. The testbench runs injection before the routers and observes the same start-of-cycle capacity on injection queues.

XY routing selects one output per visible input head. Each output applies deterministic round-robin arbitration over its connected inputs and advances the cursor only after a successful transfer. The example records injection and delivery cycles by packet ID so registration-order reversal and future schedulers can be checked against the same workload.
