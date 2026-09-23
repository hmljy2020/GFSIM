# 0009: RID-aware queue flushing

Status: provisional

## Context

Recovery must remove selected in-flight packets without disturbing unrelated queue entries. Queue entries can be staged with different remaining latencies or already visible. RID reuse after wrap makes a bare integer insufficient as an identity.

## Decision

- Introduce `RID {value, wrap, valid}` with exact equality over all fields.
- Keep predicate-based `FlushIf` and add `FlushIf(RID)` for exact identity, plus a comparator overload for caller-defined RID ordering. A packet can expose `RID rid` or supply a queue-level extractor.
- Maintain one delay counter for every staged packet. All flush paths remove staged packet and delay metadata together, then inspect visible entries. Return the number removed.

## Consequences

Exact RID flush does not guess whether a packet is younger than a recovery point. Models that need age, thread, or block context must provide that rule explicitly. The queue's delay metadata is now always aligned with staged storage, including default one-cycle entries.
