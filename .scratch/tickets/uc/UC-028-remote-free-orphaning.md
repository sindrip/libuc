---
id: UC-028
title: Remote free and heap orphaning
status: todo
depends: [UC-025, UC-026]
---

**Revision needed before implementation:** this design predates the
sender-side batching idea from the snmalloc discussion; revisit push
granularity before building.

## Goal

Free from the wrong scheduler, and let a heap outlive its scheduler.

## Spec

Non-local free pushes onto the owning shard's MPSC stack; the owner drains
at top-of-iteration (relaxed load first, `xchg` only if non-null) and on
allocation failure before mapping a region. No `MSG_RING` wakeup.

The refcount does not exist while the heap is live; it is published by a
CAS from live to `ORPHANED | count` after a final drain. The last free
unmaps. Live blocks never hold a scheduler open.

## Files

- `src/stdlib/`
- `src/uc/scheduler/`
- `test/`

## Acceptance

- A block allocated on one scheduler and freed from another is reclaimed
  by its owner without a wakeup.
- A scheduler dies with live blocks outstanding; the heap survives until
  the last free, which unmaps it.
- Both architectures build and test cleanly.
