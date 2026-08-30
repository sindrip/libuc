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

Each fiber owns one thread-local block — the root fiber included. The block
comes from `__libuc_thread_local_block_create` as a provisional owner: the
create-versus-carve decision is deliberately deferred to the stack-pooling
ticket, and `findings.md` records the constraints that keep the swap cheap.
Switching to a fiber installs that block's thread pointer as part of the
context transition — `__libuc_thread_local_block_install` is already the
bare register write, so switches call it directly.
`__libuc_thread_local_install_available` is init-time capability probing:
asked once per scheduler, failing loudly there, never per switch.
Destruction releases both the block and stack. This
retires the no-`_Thread_local` invariant: from here, constructors and `main`
run against the root fiber's own block.

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

## Landed so far (2026-08-30, UC-006-min)

Fibers own their block by value; creation binds the TCB's fiber word,
`fiber_run` saves the caller's thread pointer, installs the fiber's, and
restores on return; `__libuc_fiber_current` reads the TCB through the
installed pointer, nullptr when the register is zero. Because UC-003 already
enters `main` on the root fiber, the root block and installed-TLS `main`
landed with it — what remains here is retiring the no-`_Thread_local`
invariant formally and asserting constructors/main see initialized state in
`test/main.c`.

Cost, accepted deliberately: startup now installs before any gate can run,
so x86-64 without FSGSBASE cannot boot any libuc program — under Rosetta the
probes hang rather than trap. Emulated x86-64 behavioral acceptance is
retired; the x86-64 build tier remains compile-and-link. Behavioral
acceptance needs FSGSBASE hardware.

## Acceptance

Two fibers mutate the same `_Thread_local` variable across repeated switches,
each observing only its own value, and constructors and `main` observe
initialized independent thread-local state on the root fiber. The private
current-fiber lookup identifies each one while it runs, and the no-`PT_TLS`
probe still has no `PT_TLS` program header.
