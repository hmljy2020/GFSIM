# Module methods example

## Purpose and topology

MethodBench derives from SimTestbench and uses its final lifecycle methods. Its
Build follows the module template: declare capacity, create/wire objects, then
BuildInnerQueues and BuildChildren. Capacity is exactly three: one
[MethodTarget](MethodTarget.md) followed by two [MethodCaller](MethodCaller.md)
instances. It owns no physical queues or ports. A const target pointer bound
during Build lets the bench observe state. Each caller binds its own mutable
reference in its constructor. These three references create dashed `method`
edges and add no objects, state nodes, or worker tasks.

## Build and run

From the repository root:

```sh
cmake -S . -B build -DCMAKE_CXX_STANDARD=11 -DCMAKE_CXX_STANDARD_REQUIRED=ON
cmake --build build
./build/method_example 4 build/methods.dot
dot -Tsvg build/methods.dot -o build/methods.svg
ctest --test-dir build --output-on-failure
```

The first argument is 0 (serial), 1, 2, or 4 requested workers; the optional second
argument saves DOT. The existing scheduler caps workers at the number of direct
children (three here). The target and first caller use distinct threads with
2 or 4 requested workers. Method calls execute on callers while target Work and
Xfer keep the target's scheduled thread. Binding never changes task assignment.

The [executable source](main.cpp) is built as `method_example`; CTest runs it with
four requested workers and verifies normal completion.

## State and checking

Target initial values are (10, 20). The first caller adds 1 on cycles 1..6; the
second adds 2 on cycles 1, 3, and 5. After each Xfer, values are:

| Cycle | First | Second |
| --- | --- | --- |
| 0 | 10 | 20 |
| 1 | 11 | 22 |
| 2 | 12 | 22 |
| 3 | 13 | 24 |
| 4 | 14 | 24 |
| 5 | 15 | 26 |
| 6 | 16 | 26 |
| 7 | 16 | 26 |

CheckOutputs runs during Work after WorkChildren completes. It checks the
previous cycle's committed values, so cycle 7 verifies all six update cycles.
`PURE static Expected(field, completedCycles)` computes expected values from
explicit inputs. Each verified field records an output check and useful
progress; DriveStimuli is empty and records no stimuli. Callers mark progress
only on actual writes. No method writes, queue readiness checks, or cross-owner
reads occur during Xfer. Disabled mode performs no calls or checks.

The final inherited Work requests termination with `method_bench complete` when
Done becomes true on cycle 7. There are 14 verified outputs and zero bench-sent
stimuli. A two-cycle deadlock threshold is sufficient because enabled operation
makes progress each cycle. Disabled mode reaches no-progress deadlock on cycle
2 despite target lifecycle bookkeeping. Reset clears Done, all counters and
thread diagnostics, and restores target initial state for identical replay.

## Observability and tests

The bench logs completion at `method_bench.complete`; child method/hold logs
identify actual calls. SimTestbench reports stimuli sent and outputs checked
since reset; child reports give write counts, observation counts, and committed
values. There are no queue snapshots or asynchronous module durations in this
example. DOT shows the references and ordinary ownership clusters only.

`method_interaction` compares each cycle and reset replay in serial and 1/2/4
worker configurations; checks thread identities, old reads, next-cycle values,
untouched state, ownership, task counts, logs, reports, completion, and disabled
mode deadlock. `bind_module` additionally covers invalid and cyclic references,
optional/const targets, freeze errors, and coexistence with queue edges.
