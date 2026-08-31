---
id: UC-011
title: Pool stacks and decide block ownership
status: todo
depends: []
---

## Goal

Make fiber creation and recycling from a per-scheduler pool cheap, and settle
create-versus-carve with measurements instead of projections.

## Spec

The ownership seam comes first. Today's caller-owned
`__libuc_fiber_create(fiber, ...)` cannot reach a scheduler-owned pool, so this
ticket must make the owner explicit before adding one. Choose and record either
a scheduler-owned creation operation or a scheduler/pool argument on fiber
creation and destruction. Do not introduce an ambient process-global pool or
derive ownership from a CPU: the owner is the scheduler.

Against that explicit owner, compare the two shapes left open by the current
implementation:

- create-owns: stack and thread-local block retain separate mappings; the pool
  can recycle the stack but not absorb the block mapping;
- geometry-and-carve: one scheduler-owned allocation contains both, and recycle
  reinitializes the block with zero-fill plus the executable TLS image.

Use `../../stacks.md` for current implementation facts and questions to
measure. The constraints that keep the swap private are in `../findings.md`:
fiber code reaches TLS through the block handle and thread pointer, and the
architecture placement helper remains the sole geometry authority. Whichever
shape wins, remove mapping/alignment slack that it no longer needs.

The root fiber is not exempt. Scheduler zero exists before it is created, so it
must use the same ownership seam even if startup still supplies stable fiber
record storage. Guard pages, pool refill size, high-water retention, and release
at scheduler teardown are part of the recorded decision; dense language-only
stacks are not.

The seam's intended public face is `<threads.h>`: `thrd_t` is a fiber, and
`thrd_create` is the standard name for spawn-on-the-current-scheduler — design
the ownership API with it as the consumer rather than inventing a private
spawn surface to replace later. `thrd_create` needs the pool (no caller-owned
fiber struct in a public signature) plus an ambient-scheduler accessor (the
TCB already carries the current fiber; the scheduler can ride alongside);
`thrd_join` additionally needs exit tracking and reclaim rules, which are
UC-024's, so create and join split. `thrd_yield`, `thrd_sleep`
(`IORING_OP_TIMEOUT`), and `thrd_current` are separable cheap slices. When
`thrd_*` goes public, record the progress caveat: cooperative fibers satisfy
C11's loose forward-progress only because blocking calls suspend — vendored
code that spin-waits without a blocking call livelocks.

## Acceptance

A probe reports first-create and recycle syscall counts for both shapes. The
chosen shape lands with a per-scheduler pool; destroying and recreating a fiber
from a warm pool performs no `mmap` or `munmap`, reinitializes TLS, and never
returns storage through a different scheduler. The root fiber uses the same
ownership seam. The numbers and decision are recorded in `../findings.md`.
