---
id: UC-013
title: Park fibers on the ring
status: todo
depends: [UC-009, UC-010]
---

## Goal

Connect the two halves: a fiber's blocking request becomes an SQE plus a
park, and its completion becomes a wake — the reactor iteration.

## Spec

The request enum grows WAIT. A private fiber-side operation fills one SQE
(`user_data` is the fiber pointer), publishes WAIT, and suspends; the fiber
is not requeued. The scheduler counts it in flight — the primitive
questioned out of UC-009, arriving with the machinery that makes it true.

The loop becomes the reactor: sweep the ready queue to empty; if nothing is
in flight, return; otherwise `submit(min_complete = 1)` — one syscall
flushing every SQE the sweep batched and waiting until at least one fiber
can run — then reap all completions, store each `res` on its fiber, and
enqueue it. A batch reaching `sq_entries` mid-sweep flushes early with
`submit(0)`. Pure-yield iterations never enter the kernel.

Fibers reach their scheduler through a pointer bound at first enqueue —
the birth binding of invariant 3; rebinding is forbidden. The first opcode
is `IORING_OP_NOP`: it proves park, batch, wake, and result delivery with
no fd dependency. Real opcodes, multiple in-flight SQEs per fiber,
cancellation, eager-submit tuning, and timeouts stay outside this ticket.

## Staged landing

1. Birth binding: `fiber->scheduler` set at first enqueue. No behavior
   change; the rebind contract is the step's question.
2. WAIT unreachable: enum kind, fiber-side surface, trapping loop arm —
   covered-switch forces the arm to arrive with the enumerator. The step's
   question is the surface: prepare-then-wait versus one call.
3. The reactor tail with the acceptance probe: in_flight, park, submit,
   reap, result delivery, wake. The step's question is how `res` reaches
   the fiber.
4. Batch-overflow flush with its own forcing probe (~70 parked fibers):
   untested overflow handling is the silent-drop class again.

In-flight accounting rides with step 3, not earlier — landed alone it is
dead state.

## Files

- `src/fiber/`
- `src/scheduler/`
- `test/scheduler.c` or a new probe
- `meson.build`

## Acceptance

Fiber A records a turn then NOP-waits, three turns; fiber B records three
yielding turns then exits. The order is exactly `A0 B0 B1 B2 A1 A2`: B runs
while A is parked, A wakes only through CQEs, and the loop returns with the
queue empty and nothing in flight. Each wake delivers `res == 0` to A. Both
architectures build clean.
