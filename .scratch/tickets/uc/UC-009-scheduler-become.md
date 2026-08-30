---
id: UC-009
title: Make the current task a scheduler
status: todo
depends: [UC-007, UC-008]
---

## Goal

Make the calling Linux task scheduler zero without cloning a thread or
introducing public topology.

## Spec

Add a private become operation taking caller-owned scheduler storage. It
initializes scheduler-local state and creates the calling task's ring. It takes
no CPU argument: ring ownership is bound to the task, not to the CPU on which
the task happens to run, and placement is separate policy.

The scheduler owns a FIFO ready queue, its saved context, its ring, the current
fiber pointer, and live-fiber accounting. All are local and require no atomics.
The caller may seed ready fibers before entering the loop. Only scheduler code
mutates the ready queue.

The first loop handles only the requests UC-007 implements. YIELD appends the
fiber to the ready queue; EXIT removes it from live accounting. Fiber objects
remain caller-owned in this ticket and may be destroyed only after the loop has
returned to the scheduler stack. NONE reaching dispatch is a fatal internal
error. With no parked fibers or I/O requests yet, an empty ready queue with a
nonzero live count is likewise an internal error; zero live fibers returns to
the caller for probe use.

The scheduler path runs with the bootstrap thread pointer restored and does not
touch `_Thread_local` state. The ring is present but no fiber I/O request is
served yet. There is no scheduler registry, scheduler id, affinity operation,
clone, cross-scheduler state, public `uc_scheduler_become`, or `<threads.h>`.
Those interfaces are not declared before they can have their full contracts.

## Files

- `src/scheduler/`
- `src/fiber/`
- `meson.build`
- `test/scheduler.c`

## Acceptance

On both architectures, the current task becomes one scheduler and runs two
fibers whose yields produce the exact in-memory order `A0 B0 A1 B1 A2 B2`.
Each fiber observes its own thread-local value and current-fiber identity on
every turn. Both exit, the loop returns with zero live fibers, and their owners
destroy them from the scheduler stack. The former `no scheduler symbol` test is
retired by this ticket.
