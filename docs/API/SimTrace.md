# Trace and swimlane API

Header: `include/SimTrace.h`. Namespace: `gfsim`. Requires C++11.

GFSIM writes one streaming Perfetto-compatible JSON object with a `traceEvents` array. All timestamps are **simulation cycle numbers**. Each sampled queue contributes:

- A `queue_snapshot` instant event (`ph: "i"`) with its full visible and staged payload lists, delay counters, stall flag, capacity, queue path, and cycle.
- A `queue_depth` counter event (`ph: "C"`) with visible, staged, and total occupancy for a swimlane track.

Every physical queue is owned by a parent module and registered for tracing when that parent calls `BuildInnerQueues()` in `Build()`. Paths use the module hierarchy, such as `core/dispatch_to_issue`; repeated names receive a stable `#2`, `#3`, ... suffix. `SimSystem` rejects root queues.

## System control

| Method | Contract |
| --- | --- |
| `EnableTrace(output, sampleEvery = 1)` | Require a built system, register all known queues, and capture the current cycle. Default interval 1 captures every cycle. |
| `GetTrace()` | Return a `SimTrace*` while enabled, otherwise `nullptr`. |
| `DisableTrace()` | Close the JSON array and flush the output. Call before `Reset()` and before the output stream is destroyed. |

`Step()` captures queues **after all `Xfer()` calls** and before termination or deadlock checks. The final step is therefore included. `EnableTrace()` also captures cycle zero when called after `Build()` and before the first step. `sampleEvery > 1` reduces file size by sampling cycles divisible by that value, while retaining the initial capture.

## Payload formatting

Arithmetic and enum queues format their values; class/struct queues format the **referenced object**. Streamable `T` uses `operator<<`; non-streamable `T` requires `SetTraceFormatter(std::function<std::string(const T&)>)`. Attempting to capture a nonempty queue without a usable formatter throws. Formatters receive a const reference and must describe the payload without mutating it. Every element is recorded; snapshots are not truncated. Return valid UTF-8 for text fields and encode binary fields into readable text.

The [trace test](../../tests/trace/test_simtrace.cpp) contains a complete parent-owned queue example with both scalar and class payloads.

## Module swimlanes

`SimTrace::AddDuration(modulePath, name, startCycle, endCycle, detail)` emits a complete slice (`ph: "X"`). `AddFlow(fromModule, toModule, startCycle, endCycle, name)` emits paired dependency arrows (`ph: "s"` and `ph: "f"`). Module lanes are created when first used. Concrete modules access the optional trace through the protected `SimModule::Trace()` helper.

The trace test also covers custom payloads, counters, slices, flows, JSON escaping, and sampling.
