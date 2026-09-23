# SimTestbench API

Header: `include/SimTestbench.h`. Namespace: `gfsim`. Requires C++11.

`SimTestbench` is an abstract `SimModule` for driving stimuli and checking outputs. It imposes no port count or payload type. A concrete testbench binds its own typed queues, implements `Build()`, and supplies these hooks:

The testbench is a child of the module that owns its stimulus and observed queues. It binds one `OUTPUT` producer for each driven queue and one `INPUT` consumer for each checked queue; system build validates the matching ports.

| Hook | Responsibility |
| --- | --- |
| `DriveStimuli()` | Produce a stimulus when the target queue can accept it; call `RecordStimulus(label)` after writing. |
| `CheckOutputs()` | Consume available outputs, verify them, then call `RecordOutputChecked(label)` or `ExpectEqual(actual, expected, label)`. |
| `Done() const` | Return true only when all stimuli have been sent and all expected outputs checked. |
| `ResetBench()` | Reset local indexes and expectations for another run. |
| `ReportBench()` | Optional extra counters appended to the standard testbench report. |

`Work()` calls the drive and check hooks, traverses optional children, and requests termination when `Done()` is true. `Xfer()`, `Reset()`, and `ReportStat()` implement the standard module lifecycle. `RecordStimulus` and successful output checks mark system progress. `StimuliSent()` and `OutputsChecked()` expose those counts.

The concrete [queue testbench](../../tests/platform/test_simtestbench.cpp) sends three values to a DUT, checks outputs in order, verifies termination and reset, and proves a mismatch raises `AssertionError`.
