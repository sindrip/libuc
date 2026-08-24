---
id: UC-005
title: Bind thread-local state to a fiber
status: todo
depends: [UC-003, UC-004]
---

## Goal

Make the fiber, rather than the kernel thread, the owner of C thread-local
state.

## Spec

Each fiber owns one thread-local block. Switching to a fiber installs that
block's thread pointer as part of the context transition. Destruction releases
both the block and stack.

## Files

- `libuc/src/fiber/`
- `libuc/src/arch/*/fiber_arch.*`

## Acceptance

Two fibers mutate the same `_Thread_local` variable across repeated switches;
each observes only its own value.
