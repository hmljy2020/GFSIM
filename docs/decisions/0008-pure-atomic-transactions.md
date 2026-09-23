# 0008: PURE methods and atomic queue firing

Status: provisional

## Context

Modules often combine several queue heads into new outputs. Consuming an input before confirming all output capacity can lose work or partly apply a modeled operation. The combinational calculation should be reviewable apart from module state changes.

## Decision

- Add `PURE` as an empty method marker. Mark combinational helpers and prefer static methods with explicit const inputs and no queue or module-state access.
- A module fires only when every required input is nonempty, every selected output has capacity, and any additional guard is satisfied.
- Before firing, inspect input heads with `Front()` and compute outputs locally. Write admitted outputs before popping consumed inputs. Record progress only after commit.
- When blocked, leave all queue contents and module transaction state unchanged; only non-consuming peeks are permitted.

## Consequences

This is atomicity at the simulation protocol level under normal queue operations. It does not promise rollback of a host allocation failure partway through several output writes. Reviewers can identify side-effect-free calculations by the `PURE` marker and verify fire gates independently from computation.

Forwarding a class message with `Front()` → output `Write()` → input `Pop()` briefly copies the `shared_ptr` handle. It does not copy the message body, and it preserves the input if the output write cannot complete.
