# SimObj and SimSys API

Header: `include/SimSystem.h`. Namespace: `gfsim`. Requires C++11.

`SimSys` is an alias of `SimSystem`; both names provide the same interface. See [decision 0001](../decisions/0001-simulation-foundation.md) for the cycle contract.

For nested modules and typed queue ports, see the [SimModule template](SimModule.md).

## SimObj

Construct each `SimObj` with a nonempty name and implement its five pure virtual methods. Its destructor is virtual.

| Method | Contract |
| --- | --- |
| `SimObj(std::string name)` / `Name() const` | Store and read a stable, nonempty object name. |
| `void Build()` | Create or validate connections before simulation begins. |
| `void Work()` | Evaluate the current step. Called once per registered module by `Step()`. |
| `void Xfer()` | Complete the phase after every registered module has run `Work()`. |
| `void Reset()` | Clear module state; called by `SimSystem::Reset()`. |
| `void ReportStat()` | Report module statistics; called by `SimSystem::ReportStat()`. |
| `SimSystem* GetSim()` | Returns `nullptr` by default; a module can override it to expose its owner. |

## SimSystem / SimSys

| Method | Contract |
| --- | --- |
| `void AddModule(std::shared_ptr<SimObj> module)` | Register a non-null root module before build. A physical queue cannot be a root. Registration order determines call order. |
| `void Build()` / `bool IsBuilt() const` | Build every root once, freeze each object's declared collection, validate queue ownership and one-to-one ports, then reset state. `Step()` requires a successful build. |
| `void Step()` | Increment the cycle count and logger cycle, call every `Work()`, then every `Xfer()`, then check deadlock. |
| `void Run(uint64_t maxSteps)` | Step until termination; throw if the positive step limit is reached first. |
| `void Reset()` | Clear cycle, termination, deadlock, and stalled-cycle state, then reset all registered objects. The build remains valid. |
| `void ReportStat()` | Call every module's `ReportStat()` in registration order. |
| `std::uint64_t Cycles() const` | Return the number of steps since construction or the last reset. |
| `void RequestTerminate(std::string reason)` / `NeedTerminate()` / `TerminationReason()` | Request and inspect a completed simulation. `Step()` rejects further calls after termination. |
| `void MarkProgress()` | Mark useful work during a step. This resets the stalled-cycle count after the step. |
| `void SetDeadlockThreshold(uint64_t cycles)` / `DeadlockThreshold()` | Configure consecutive no-progress cycles before deadlock. Zero disables the check. |
| `void CheckDeadlock()` / `StalledCycles()` / `IsDeadlocked()` | Check and inspect the no-progress state; throw `DeadlockError` when the threshold is reached without termination. |
| `Reporter& GetReporter()` | Return the system's report formatter. |
| `Logger& GetLogger()` | Return the system's cycle-aware logger. |
| `SimMemory& GetMemory()` | Return the system-owned guest memory. System reset preserves its contents. |
| `EnableTrace(output, sampleEvery = 1)` / `DisableTrace()` | Start and close streaming queue snapshots and swimlane events. Disable before reset. |
| `SimTrace* GetTrace()` | Access optional module event recording. Owned inner queues register during build. |
| `WriteDot(output)` / `SaveDot(path)` | Export the built module/queue topology as nested Graphviz DOT. |

Progress means an accepted transfer, completed operation, or other forward movement toward termination. Mark it at the module that owns the event. Queue bookkeeping alone does not mark progress. See [decision 0004](../decisions/0004-build-termination-deadlock.md).

See the [trace API](SimTrace.md) for queue snapshots, sampling, module slices, and flows.

`SimSystem` has a parameterless constructor. Connections and object counts are fixed by the modules' `Build()` methods. Each physical queue must belong to a parent module and connect one output to one input; cycles are allowed. See [decision 0007](../decisions/0007-payload-policy-and-fixed-topology.md).

Example:

```cpp
#include <memory>
#include "SimSystem.h"

class Counter : public gfsim::SimObj {
public:
    Counter() : SimObj("counter") {}
    void Build() override {}
    void Work() override { ++count; }
    void Xfer() override {}
    void Reset() override { count = 0; }
    void ReportStat() override {}
    int count = 0;
};

int main()
{
    gfsim::SimSys system;
    auto counter = std::make_shared<Counter>();
    system.AddModule(counter);
    system.Build();
    system.Step();
    return counter->count == 1 && system.Cycles() == 1 ? 0 : 1;
}
```

## Parallel child subtrees

Configure execution before `Build()`:

| Method | Contract |
| --- | --- |
| `void SetWorkerCount(std::size_t count)` | Positive worker count, default 1. Values greater than 1 require a split parent. |
| `void SetParallelChildren(SimModule* parent)` | Non-null, non-owning pointer to a module in this system's built hierarchy whose `GetSim()` returns this system. |

Both setters reject changes once Build has started, including after a failed
Build. The parent can be nested and must be available to the application before
Build. Without a split, traversal retains its serial behavior. With a split,
each direct child is one complete task subtree, assigned in registration order
to worker `childIndex % min(workerCount, childCount)`. Each worker's tasks run in
registration order. Zero children means zero workers and an empty dispatch;
one worker still uses the same phase and termination contract as multiple workers.

Workers are created after build validation and initial reset, reused across
Reset, and joined before the system destroys its modules and queues. The split
parent executes on the main thread. Queue advancement is assigned independently
of physical ownership as described below. Its child traversal helpers
wait for all dispatched tasks. All roots finish Work before any root starts
Xfer; all Xfer calls finish before the next cycle. Within a subtree, traversal
remains sequential. A testbench still drives stimuli before its children and
checks outputs after they finish.

With a split configured, termination requests during Step take effect at the
end of the cycle, after Xfer and trace capture. `NeedTerminate()` and
`TerminationReason()` expose only the previously published state during the
cycle. Choose the reason by Work before Xfer, then lowest task ID: main thread
is 0, direct children are 1..N independently of worker count. Within that
phase/task, keep the first request. Requests outside Step and systems without a
split retain immediate termination behavior. Useful work can call
`MarkProgress()` concurrently; its relaxed atomic flag does not synchronize
model data. The phase barriers provide that synchronization.

An exception in a worker skips that worker's remaining tasks in the batch. All
other workers finish before the first recorded exception is rethrown on the
main thread. Work failure prevents Xfer. Any failed parallel Step requires a
successful Reset before further stepping; pending termination requests are
discarded. Reset does not roll back guest memory or external side effects.
Failed Reset leaves the execution failure state set. Serial exception behavior
is unchanged. Partial worker creation is cleaned up before Build throws.

The application owns the control thread: do not call Step, Reset, ReportStat,
configuration, trace setup/close, graph export, or destruction concurrently
with model execution. Reporters and guest memory are not made thread-safe by
the executor. See [queue restrictions](SimQueue.md#parallel-access-contract)
and [decision 0013](../decisions/0013-parallel-subtrees.md).


### Queue assignment within the existing Xfer phase

Build maps each module below a split child to that child's stable task ID. Each
physical queue follows its producer task; if the producer is outside the worker
subtrees, it follows its consumer task. If neither endpoint is in a worker
subtree, the queue stays on the existing owner traversal. Queues are appended
in structural owner/registration order, making assignment independent of hash
iteration order. Assignment is fixed until system destruction and reused by
Reset. No new public configuration is required.

Each worker Xfer task first advances its assigned queues, then calls its subtree
root's Xfer. `AdvanceInnerQueues()` skips these delegated queues so each queue
advances exactly once. Physical ownership, Objects(), DOT, trace paths, and
owner-based Reset remain unchanged. A parent may own a queue that executes in
another root's worker subtree. Queue failures follow the same exception/reset
contract as module failures.

Module Xfer commits private state and must not inspect queue capacity/contents
or other modules' concurrently committed state. Perform those decisions during
Work. There is no additional queue-publication barrier: all module Work finishes
before any queue advancement, and every queue and module Xfer finishes before
the next Work. In particular, the legacy queue method named Work performs its
publication in the system Xfer phase; it cannot run concurrently with module
Work. Parent pre/post traversal and testbench stimulus/check ordering are still
preserved. See [decision 0014](../decisions/0014-queue-task-affinity.md).
