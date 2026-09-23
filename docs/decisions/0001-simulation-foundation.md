# 0001: Simulation foundation

Status: provisional

## Context

GFSIM needs a small simulation interface, deterministic cycle scheduling, and queues that distinguish staged writes from readable entries. Modules must be usable without ISA-specific types.

## Decision

- Define named `SimObj` instances with `Build`, `Work`, `Xfer`, `Reset`, `ReportStat`, and optional `GetSim` methods.
- Let `SimSystem` own registered modules. Build all roots before running. Each step increments the cycle count, calls `Work()` on every module, then calls `Xfer()` on every module.
- Provide `SimSys` as a short alias for `SimSystem`.
- Give `SimQueue<T>` staged write storage, visible read storage, capacity bounded by 512, latency, and stall control. Arithmetic and enum `T` values are stored directly; class/struct `T` values are stored as `std::shared_ptr<T>`. Its `Work()` advances entries between phases.
- Keep module registration order explicit. A queue registered before its producer advances earlier in the Work phase; parent-owned inner queues advance during the parent's Xfer phase.

## Consequences

The platform remains C++11 compatible and has no ISA dependencies. A `SimQueue` can expose a newly written entry in the same step if its `Work()` runs after its producer; topology owners must choose queue registration order deliberately. `Front()` returns by value, so peeking requires a copyable payload. Raw queue storage access can bypass normal capacity and delay bookkeeping.
