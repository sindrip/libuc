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

Fiber creation binds the TCB's runtime-owned fiber word to its owner. A private
current-fiber lookup reads that word through the installed thread pointer;
there is no process-global `current_fiber` and no libuc `_Thread_local`
variable whose mere presence would force a `PT_TLS` segment. Returning to the
bootstrap context restores its absent thread pointer, so bootstrap code remains
unable to use `_Thread_local` state.

## Files

- `src/fiber/`
- `src/arch/*/fiber_arch.*`
- `src/start.c`
- `test/main.c`

## Acceptance

Two fibers mutate the same `_Thread_local` variable across repeated switches,
each observing only its own value, and constructors and `main` observe
initialized independent thread-local state on the root fiber. The private
current-fiber lookup identifies each one while it runs, and the no-`PT_TLS`
probe still has no `PT_TLS` program header.
