# SimState API

Header: `include/SimState.h`. Namespace: `gfsim`. Requires C++11.

`SimState<T>` is a header-only final class derived directly from `SimObj`.
One instance holds one logical field. Modules can embed independently typed
members and fixed arrays without generating concrete State classes.

| Interface | Contract |
| --- | --- |
| `SimState(std::string name, const T& initialValue)` | Stable nonempty name; explicit initial value saved by copy for Reset. |
| `T Read() const` | Inline current value returned by value; no mutable storage references. |
| `void Write(const T& value)` | Inline pending assignment plus this instance's written flag. |
| `Build()` | Calls `SetObjectCapacity(0)` once. |
| `Work()` | Empty; the owner skips it. |
| `Xfer()` | Commits pending only if written, then clears the flag. |
| `Reset()` | Restores the saved initial value and clears the flag, discarding uncommitted changes. Pending need not be cleared. |
| `ReportStat()` | Empty; the owner calls it explicitly. |

Private storage is `initial_`, `current_`, `pending_`, and an ordinary `bool written_`.
All three values are initialized from the constructor argument. T must be copy
constructible and copy assignable; it need not be default constructible.
Arithmetic types, bool, enums, and copyable value structs are supported.
`SimState<Data>` treats the entire struct as one field, not independently writable
members. Unlike queue class payloads, T is not automatically wrapped in shared_ptr.
Copying a pointer or shared handle does not isolate or deep-copy its target.

Within Work every read sees the old current value, including after Write.
Sequential repeated writes use the last value; unwritten instances retain their
current value. Calling `count.Write(count.Read() + 1)` twice in the same Work
increments only once. There is no implicit accumulation.

## Ownership and scheduling

Embed states as private module members. The owner explicitly calls Build, Xfer,
Reset, and ReportStat on every instance and fixed-array element. Each has a stable
nonempty name and commits exactly once per cycle on the owner's execution thread.
Build first declares the exact number of direct child modules plus owned queues,
then builds its states, creates/wires children and queues, and calls
BuildInnerQueues and BuildChildren. Xfer commits states before AdvanceInnerQueues
and XferChildren. Reset resets states before ResetInnerQueues and ResetChildren.
ReportStat reports meaningful committed values through Read, visits the states,
then calls ReportChildren. Build is not repeated on Reset.

Do not register state in Objects, Submodules, queue collections, or system roots.
Exclude it from object capacity. It has no DOT node or independent worker task,
and no automatic Parent/GetSim context (GetSim returns null). System traversal
does not freeze or validate embedded states; the owner guarantees fixed shape,
lifetime, and lifecycle correctness.

Current remains read-only during Work. Other modules may use typed non-owning
constructor pointers or ordinary module methods to access state. Methods execute
synchronously on the caller's thread. Different workers may write different
instances. The model must prevent concurrent writes to the same instance, even
when writing identical values or different members of a struct T. Sequential
last-write behavior does not define a winner for concurrent writes.

Other tasks must not access an owner's state during Xfer. Existing barriers
ensure all Work completes before commits and all Xfer completes before the next
Work. No locks, atomics, conflict arbitration, or scheduler changes are added.
Packet transport continues through queues; method calls add no queue safety.

Each Xfer checks one bool. An owner visits N instances in O(N), copying only
written values. Reset retains an initial-value copy. Each instance also carries
SimObj name/metadata overhead; adjacent instances can share cache lines. There
is no dirty list, shared bitmap, per-Write framework allocation, or padding.
See [decision 0016](../decisions/0016-embedded-state.md).

## Executable example

This example directly includes the public header. CTest compiles and runs the
matching [source](../../tests/state/state_api_example.cpp) as `state_api_example`.

```cpp
#include <cstdint>
#include "SimState.h"
#include "SimAssert.h"

int main()
{
    gfsim::SimState<std::uint64_t> pc("example.pc", 0x1000);
    gfsim::SimState<unsigned> count("example.count", 0);
    pc.Build();
    count.Build();
    pc.Write(0x1004);
    count.Write(1);
    count.Write(2);
    GFSIM_ASSERT(pc.Read() == 0x1000 && count.Read() == 0);
    pc.Xfer();
    count.Xfer();
    GFSIM_ASSERT(pc.Read() == 0x1004 && count.Read() == 2);
    pc.Write(0x1008);
    pc.Reset();
    count.Reset();
    pc.Xfer();
    count.Xfer();
    GFSIM_ASSERT(pc.Read() == 0x1000 && count.Read() == 0);
}
```

The [module template](SimModule.md) requires only `@MODULE_NAME@` and uses a
SimState counter directly. [StateOwner](../../tests/state/StateOwner.md) demonstrates
heterogeneous instances, arrays, and two workers writing separate instances.
`simstate` compares serial, one-worker, and two-worker results every cycle,
including reset replay, completion, no-progress deadlock, and topology exclusion.
For module-level reference registration and DOT method edges, use
[SimModule::BindModule](SimModule.md#module-references-and-ordinary-methods).
SimState itself remains unregistered. Lifetime validation and thread assignment
based on reference edges are not provided.
