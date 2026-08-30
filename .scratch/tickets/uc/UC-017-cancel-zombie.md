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

Open. The load-bearing kernel fact: a cancel CQE does not prove the
original operation is dead — the kernel separately queues the original
request to complete with `-ECANCELED` (`out/src/io_uring/cancel.c:476`),
so releasing the slot on the cancel CQE recycles it while a CQE is still
inbound. The zombie path: fiber exits → mark ZOMBIE → submit cancellation
→ reap the cancel CQE → reap the original's terminal CQE or notification
→ `active_ops` reaches zero → reclaim stack, thread-local block, and
operation slots.

Two structural consequences. The scheduler must stay alive while zombies
or active operations exist: `ready + parked == 0` stops being a valid
termination test by itself. And the generation field earns its keep here:
a stale `{slot, generation}` from a late CQE must fail validation rather
than address the slot's next tenant.

## Acceptance

- Fiber exit cancels and drains the original operation before any
  reclamation.
- A CQE arriving after the zombie drain cannot address the slot's next
  tenant.
- The loop does not return while a zombie holds operations.

Both architectures build clean.
