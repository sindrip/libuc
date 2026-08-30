---
id: UC-017
title: Cancellation, zombies, and slot recycling
status: todo
depends: [UC-016]
---

## Goal

Let a fiber stop owning operations safely: cancellation as a protocol,
exit while operations stand, and generation-safe recycling of operation
slots.

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
- A stale generation cannot address a recycled slot.
- The loop does not return while a zombie holds operations.

Both architectures build clean.
