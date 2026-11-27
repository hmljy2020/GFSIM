# 0012: Queue slots freed during Work become writable next cycle

A full queue previously accepted a replacement in the same cycle if its consumer ran first. Reversing module registration order could then change the throughput of the same pipeline.

During module Work, the producer counts occupied slots from the last queue boundary plus its own successful writes. All admission checks use this count, including reservations for multiple writes. The original implementation allowed consumers to remove visible entries immediately; [decision 0019](0019-delayed-queue-consumption.md) supersedes that behavior with one deferred consumption request per cycle, preserving this write budget. The queue refreshes the count when its parent advances it during Xfer, including when a stall suspends packet delivery. Reset clears the count along with the data.

A depth-one queue thus alternates between filling and draining under continuous demand; a depth-two queue with one visible entry has room for a simultaneous write. This decision covers ordinary queue admission and preserves staged-data latency. It does not introduce a parallel scheduler or synchronization for recovery, inspection, or raw container access.
