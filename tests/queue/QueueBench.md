# QueueBench

Template-derived SimTestbench fixture with two owned queues and four QueueView
children (object capacity six). The bench produces six immutable messages on
`input` (depth three, latency two); `consumer` forwards them to `output` (depth
one, latency one), which the bench checks. Three portless observers bind const
method references to the consumer. Queue advancement follows the consumer worker
for input and the producer worker for output; both queues remain bench-owned.

DriveStimuli allocates only admitted messages, and CheckOutputs verifies pointer
identity and value before requesting Pop. Output checking starts at cycle eight
to create backpressure, or remains disabled for the deadlock test. Records count
only successful sends/checks and mark useful progress. Completion uses six
verified transactions, without relying on same-cycle Empty. A five-cycle
no-progress threshold detects the deliberately blocked case. Normal tests use a
threshold of sixteen to allow the initial blocked interval.

SimTestbench supplies the five lifecycle methods except Build, invoking the
standard child/queue helpers. ResetBench clears retained source handles; the
base clears counters, queues, and children. Reports/logs use the base testbench
contract and QueueView counters. Queue trace formatters emit the immutable value;
no additional timed operation is modeled. Tests compare every cycle across
serial and 1/2/4 workers, replay after reset, and render the fixed DOT topology.
