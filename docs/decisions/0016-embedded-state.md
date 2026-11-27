# 0016: Field-level SimState with owner-managed commits

## Decision

Provide a C++11 header-only `SimState<T>` directly derived from SimObj, with one
logical field per instance. This replaces concrete State class generation.
Each instance stores a constructor-supplied initial value, current, pending,
and an independent ordinary bool written flag. T supports copy construction and
assignment; no default constructor is required. Inline Read returns current by
value; Write assigns pending and sets the flag. Struct values commit as a whole,
without queue-style shared_ptr conversion or per-member concurrency.

Build declares zero object capacity. Work and ReportStat are empty. Xfer commits
only written instances and clears their flags. Reset restores the saved initial
value and clears the flag; pending need not be cleared. Reads after writes see
old current. Sequential repeated writes use the last value, so repeated
Write(Read()+1) does not accumulate. Unwritten instances hold their values.

Owners embed private members or fixed arrays, name every instance, explicitly
call Build, Xfer, Reset, and ReportStat, and commit each once per cycle on their
own thread. States are excluded from Objects, Submodules, queues, system roots,
object capacity, DOT, and worker tasks. They have no automatic Parent/GetSim
context or system freeze/validation. Owners guarantee fixed shape and lifetime.

During Work, workers may read current and write distinct instances. Models must
prevent concurrent writes to the same instance, including different members of
one struct value. Ordinary typed constructor pointers or module methods provide
access on the caller's thread. Only the owner accesses state in Xfer. Existing
Work/Xfer barriers suffice; no locks, atomics, or scheduler changes are needed.

## Consequences and boundaries

The module template uses a SimState counter and preserves counting, logging, and
reporting. Tests use the public header directly; no concrete State generation or
extra implementation file is required. Existing business examples and queue
semantics remain unchanged. Reference-binding APIs, method graph edges, automatic
lifetime checks, and thread assignment based on method references are deferred
by this decision. [Decision 0017](0017-module-method-bindings.md) subsequently
adds module reference registration and method graph edges; it leaves automatic
lifetime checks and reference-based thread assignment out of scope.

Owner traversal costs O(N) for N instances, with one flag check per instance and
copies only for written values. Each instance retains an initial-value copy and
SimObj metadata/name, costing more than grouping fields in one object. Copy cost
for composite values depends on T; shared handles do not deep-copy their targets.
No dirty list, bitmap, framework allocation per Write, or cache-line padding is
introduced; distinct instances can still share cache lines.

## Validation

Test scalars, bool, enums, nonzero initial values, and a non-default-constructible
value struct. Check old reads, last write, no implicit accumulation, unwritten
values, whole-value commits, and pending discard on Reset. Exercise heterogeneous
members and fixed arrays through owner lifecycle methods. Compare serial,
one-worker, and two-worker execution cycle by cycle and after reset replay,
without deliberately introducing data races. Check topology/task exclusion,
logs/reports, normal termination, and no-progress deadlock.

Compile documentation and module templates, run a clean C++11 build and full
CTest suite, inspect/render DOT, and run related ThreadSanitizer tests when
supported.
