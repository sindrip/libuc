---
id: UC-013
title: Park fibers on the ring
status: done
depends: [UC-009, UC-010]
---

## Goal

Connect the two halves: a fiber's blocking request becomes an SQE plus a
park, and its completion becomes a wake — the reactor iteration.

## Spec

The request enum grows AWAIT. A blocking caller builds one SQE in its own
frame and hands it to `__libuc_fiber_await`, which stores the pointer,
publishes AWAIT, and suspends; the parked stack keeps the SQE alive, and
the fiber is not requeued. The loop copies it into the SQ slot, stamps
`user_data` with the fiber pointer. Termination tracks where fibers
stand — a `ready` count on the queue and a `parked` count on the ring,
both maintained by structural moves (enqueue, dequeue, park, wake) — and
the loop returns when both are zero. Multi-CQE operations keep that true
only through UC-016's routing layer; alone, the counters assume the
single-shot contract the reap loop traps on. Settled 2026-08-30 after landing, via an in-flight
counter and then a live/queued pair; findings.md carries the arc. This
reverses UC-009's expectation that in-flight accounting would be needed
for the ring anyway; it never was (submission uses the ring's own
batch_count).

The loop becomes the reactor. UC-013 initially swept the ready queue to empty;
UC-015 now snapshots one ready generation so a persistent yielder cannot starve
completions. If no fiber remains, return; otherwise submit the batch, waiting
for one completion only when the ready queue drained, then reap all available
completions, store each `res` on its fiber, and enqueue it. A batch reaching
`sq_entries` mid-generation flushes early with `submit(0)`. Pure-yield
iterations never enter the kernel.

Fibers reach their scheduler through their resumer: AWAIT rides the request
channel YIELD and EXIT already use, so fiber-land stores no scheduler
reference and the SQ keeps one writer, the loop. Decided 2026-08-30 over
the birth-binding pointer; the enqueue-time migration tripwire goes with
it, so invariant 3 holds by convention at this layer — where it gets teeth
is a question for the ticket that introduces a second scheduler. The
in-kernel BPF loop was the tiebreak: with loop ops installed, enter stops
submitting (`out/src/io_uring/io_uring.c:2618-2621`), which revokes
fiber-side submission anyway (`.scratch/bpf-loop.md`). The first opcode
is `IORING_OP_NOP`: it proves park, batch, wake, and result delivery with
no fd dependency. Real opcodes, multiple in-flight SQEs per fiber,
cancellation, eager-submit tuning, and timeouts stay outside this ticket.
So does sweep fairness — a persistent yielder starving in-flight
completions — which is UC-015.

## Staged landing

1. AWAIT unreachable: enum kind, the pointer member, `__libuc_fiber_await`,
   trapping loop arm — covered-switch forces the arm to arrive with the
   enumerator. Decided 2026-08-30: one call taking a caller-stack SQE —
   the parked frame is the storage, so no template member exists and
   fiber-land never learns the SQE layout.
2. The reactor tail with the acceptance probe: the live count, the copy
   through `await_sqe` into the SQ, park, submit, reap, result delivery,
   wake. The scheduler writes `res` into the parked fiber before resuming it;
   UC-016 later moves completion identity and delivery into operation records.
3. Batch-overflow flush with its own forcing probe (~70 parked fibers):
   untested overflow handling is the silent-drop class again.

Fiber accounting rides with step 2, not earlier — landed alone it is
dead state.

## Files

- `src/fiber/`
- `src/scheduler/`
- `test/scheduler.c` or a new probe
- `meson.build`

## Acceptance

Fiber A records a turn then NOP-waits, three turns; fiber B records three
yielding turns then exits. With UC-015's generation bound, the exact order is
`A0 B0 B1 A1 B2 A2`: B runs while A is parked, each wake arrives through a
CQE one generation after its park, and the loop returns with the queue empty
and nothing parked. Each wake delivers `res == 0` to A. Both architectures
build clean; behavioral execution follows UC-012's architecture policy.
