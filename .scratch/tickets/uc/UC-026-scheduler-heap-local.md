---
id: UC-026
title: Scheduler-owned heap, local path only
status: todo
depends: []
---

**Revision needed before implementation:** the no-reentrancy premise below
is invalidated by the signal-preemption decision. A signal can now land
mid-allocator, so the pinned non-reentrant section and its PC-range guard
must be folded into this design first — and that guard constrains LTO: the
allocator can never inline into user text. Landing preemption also amends
AGENTS invariant 7, which still reads cooperative-only.

## Goal

`malloc`/`calloc`/`realloc`/`free`/aligned allocation for the
single-scheduler case.

## Spec

Regions come from `mmap` with metadata at the region base, so an address
alone identifies its owner and blocks need no header. The local path takes
no atomics and no lock — provable because one task owns a scheduler and
the allocator contains no suspension point.

The heap is a distinct object from the scheduler from day one so UC-028
does not have to retrofit it. Deliberately does not decide fiber arenas.

## Files

- `include/stdlib.h`
- `src/stdlib/`
- `test/`

## Acceptance

- Allocation, reallocation, and free round-trip under a probe exercising
  size classes and alignment.
- The local path contains no atomic operations and no suspension point.
- Both architectures build and test cleanly.
