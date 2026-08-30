---
id: UC-017
title: Cancellation, zombies, and slot recycling
status: todo
depends: [UC-016]
---

## Goal

Make operation teardown automatic at fiber death: exit-time cancellation,
the zombie lifetime, and delayed reclamation. Manual cancel-and-drain and
ordinary terminal recycling land in UC-016; this ticket makes them
survive an owner that is already gone.

## Spec

Open. The load-bearing kernel fact: a cancel CQE reports the cancel request's
result, not the original request's terminal event
(`out/src/io_uring/cancel.c:210-244`). A cancelled poll request is separately
kicked through its own completion path (`out/src/io_uring/poll.c:382-386`),
and queued io-wq work is separately failed with `-ECANCELED`
(`out/src/io_uring/io_uring.c:1489-1493`). Therefore releasing the original's
slot merely because the cancel CQE arrived can recycle it while its terminal
CQE is still inbound.

The two CQEs have no assumed arrival order. The original may finish before the
cancel request, and the cancel may consequently report `-ENOENT`; or the cancel
may succeed before the original posts `-ECANCELED`. The operation state machine
must accept both and retire each record exactly once.

The zombie path: fiber exits → mark ZOMBIE → submit cancellation for every
active owned operation → observe each original operation's terminal CQE (and
every required notification) → `live_ops` reaches zero → reclaim stack,
thread-local block, and operation slots. Cancel-operation records have their
own ordinary terminal CQEs and lifetime; they are bookkeeping, never proof that
the target record is reclaimable.

Two structural consequences. The scheduler must stay alive while zombies
or active operations exist: `ready + parked == 0` stops being a valid
termination test by itself. And the generation field earns its keep here:
a stale `{slot, generation}` from a late CQE must fail validation rather
than address the slot's next tenant.

## Acceptance

- Fiber exit cancels and drains the original operation before any
  reclamation, under both cancel-first and original-first CQE orderings.
- A CQE arriving after the zombie drain cannot address the slot's next
  tenant.
- The loop does not return while a zombie holds operations.

Both architectures build clean.
