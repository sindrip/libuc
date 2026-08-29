---
id: UC-006
title: Bind thread-local state to a fiber
status: todo
depends: [UC-002, UC-005]
---

## Goal

Make the fiber, rather than the kernel thread, the owner of C thread-local
state.

## Spec

Each fiber owns one thread-local block — the root fiber included. Switching
to a fiber installs that block's thread pointer as part of the context
transition. Destruction releases both the block and stack. This retires the
no-`_Thread_local` invariant: from here, constructors and `main` run against
the root fiber's own block.

## Files

- `src/fiber/`
- `src/arch/*/fiber_arch.*`
- `src/start.c`
- `test/main.c`

## Acceptance

Two fibers mutate the same `_Thread_local` variable across repeated switches,
each observing only its own value, and constructors and `main` observe
initialized independent thread-local state on the root fiber.
