# StateOwner

Adapted from `templates/SimModule.{h,cpp,md}.in`. This zero-port test model owns
two StateAccess children and seven embedded SimState instances: uint64_t
shared_pc/shared_count, uint64_t auxiliary_count, two uint64_t bank_pc elements,
and two uint32_t bank_count elements. Every field resets to zero.
Build declares exactly two objects, constructs and wires the access modules
using plain non-owning constructor pointers, then builds queues and children.
There are no physical queues, payloads, blocked outputs, or queue transactions.

The shared pc/count instances each have one writer: pc_writer and count_writer. Auxiliary
count records Work calls. Each bank increments pc by
its index plus one every cycle, and count by its index plus three on even cycles.
NextCount is a PURE static increment using only its explicit scalar argument.
Work stages bookkeeping, calls WorkChildren, then checks that shared current
values still match their pre-Work values. Xfer explicitly commits all seven
states once, advances inner queues, then calls XferChildren. Reset and ReportStat
explicitly visit every state and use the standard child helpers. Build is not
repeated on Reset. State commits scan all fields, O(total fields) per cycle.

Serial mode traverses the access modules directly. Parallel mode selects this
owner as the split parent, so two workers can access separate SimState instances
while the owner remains on the main thread. Current values remain read-only
during Work. The Work barrier precedes owner commits; access modules never read
state in Xfer. State objects are not registered, counted, frozen, validated,
given Parent/GetSim context, rendered in DOT, or scheduled independently.

`state_owner.work` logs the next cycle count. The report counter `Owner work
cycles (cycles since reset)` reports committed auxiliary count. Useful progress
is marked only by successful field staging in the children. On the configured
final Work call (32 by default), RequestTerminate("state sample complete")
allows that cycle's Xfer to finish; zero disables completion. A deadlock threshold
of two detects disabled writers while allowing continuous normal progress.
Cycle bookkeeping alone is not useful progress.

No queue snapshots, payload formatters, timing slices, or dependency flows are
needed. DOT contains the owner and its two children only. `simstate` checks field
values every cycle, single/multiple-worker thread assignments, distinct array
behavior, reset replay, child ownership, logs/reports, termination, deadlock, and
absence of state objects in topology. `state_api_example` covers the API snippet.
