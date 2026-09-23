# 0005: Memory, testbench, and assertions

Status: provisional

## Context

GFSIM needs guest memory, executable image loading, reusable stimulus and output verification, and invariant failures that carry useful source context. These facilities must stay independent of ISA-specific types and external libraries.

## Decision

- `SimMemory` uses exact mapped intervals with lazily allocated data pages. Scalar accesses use little-endian byte order. `LoadElf` validates ELF32/ELF64 little-endian program headers and loads `PT_LOAD` segments transactionally. `SimSystem` owns one memory instance.
- `SimTestbench` derives from `SimModule` and owns no fixed ports. Concrete benches implement drive, check, completion, and reset hooks. Successful stimulus writes and output checks mark progress; completion requests system termination.
- `GFSIM_ASSERT` provides a stream-style invariant check that throws `AssertionError` with expression and source location. It remains active regardless of build mode.

## Consequences

The ELF loader maps images but does not relocate them or set up a process runtime. System reset preserves memory; memory reset clears it explicitly. Testbench authors must define the expected output order and only report a checked output after validation succeeds. Assertions are for broken invariants, while malformed user inputs use ordinary exceptions.
