# 0003: System-owned reporting and logging

Status: provisional

## Context

Every module needs a consistent way to report counters and emit cycle-aware diagnostic events. Output should be capturable in tests without replacing process-wide streams.

## Decision

`SimSystem` owns one `Reporter` and one `Logger`. `Step()` updates the logger cycle before module work; `Reset()` returns it to zero. `SimModule` exposes them through `Report()` and `Log()`. Concrete modules write statistics in `ReportStat()` and meaningful events in their lifecycle methods. The reporter offers title, value, average, percentage, and combined value/percentage lines. The logger offers stream-style Debug, Info, Warn, and Error lines with cycle, unit, and stage fields. Both accept a non-owning `std::ostream` sink that tests and applications may replace.

## Consequences

Modules do not write directly to global output streams. Report division by zero produces `N/A`. The logger defaults to Warn severity. Callers must keep a replacement output stream alive while it is attached to a system.
