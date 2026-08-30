---
id: UC-010
title: Run main on scheduler zero
status: done
depends: [UC-003, UC-006, UC-009]
---

## Goal

Make process startup use the first scheduler while preserving the root fiber
and every startup contract already established.

## Spec

After parsing the initial stack, auxv, and the executable's thread-local image,
startup creates caller-owned scheduler storage on its existing frame and makes
the initial Linux task scheduler zero. It then creates the root fiber, enqueues
it, and enters the scheduler loop. Constructors and `main` continue to run on
the root fiber's mapping and thread-local block.

The kernel-provided initial userspace stack ceases to be bootstrap-only: its
live startup frame is scheduler zero's control stack. It owns no fiber-local
state, and its absent thread pointer is restored whenever a fiber suspends.

Returning from `main` records its status and makes the root fiber publish EXIT.
Because startup has not exposed fiber spawning yet, zero live fibers returns
the loop to startup; startup destroys the root fiber from the scheduler stack
and returns the recorded status to `_start` for `exit_group`. PID 1's eventual
resident shutdown policy, scheduler-local spawn, fiber I/O, and additional OS
threads remain outside this ticket.

## Files

- `src/start.c`
- `src/scheduler/`
- `src/fiber/`
- `test/main.c`
- `test/no_thread_local.c`
- `meson.build`

## Acceptance

On both architectures, constructors and `main` still run on the root fiber's
mapping with initialized independent thread-local state, and `main`'s status
still reaches `exit_group`. The no-`PT_TLS` executable retains no `PT_TLS`
program header while running with a TCB-backed root fiber. Scheduler zero's
ring belongs to the startup task, the root fiber exits through scheduler
dispatch, and its stack and thread-local block are destroyed only after control
has returned to the scheduler stack.

2026-08-30: done on aarch64 — every probe boots through scheduler zero:
become on the startup frame, root fiber enqueued and dispatched by the
loop, destroyed after it returns, status to exit_group. Full sweep green
(nine probes, containers and VM; exit-status carries 42); constructors and
main keep the root block, the no-PT_TLS header contract holds. Accepted
with sign-off: every libuc executable now requires io_uring_setup at boot,
so container runs need seccomp=unconfined — Docker's default denies
io_uring since 2023 because opcodes bypass syscall filtering, which is the
ring-as-syscall-ABI property itself. x86-64 is compile-and-link per UC-012.
