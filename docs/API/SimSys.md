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
