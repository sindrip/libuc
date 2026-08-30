---
id: UC-009
title: Make the current task a scheduler
status: done
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

The scheduler owns a FIFO ready queue, its saved context, and its ring. All
are local and require no atomics. The caller may seed ready fibers before
entering the loop. Only scheduler code mutates the ready queue.

The first loop handles only the requests UC-007 implements. YIELD appends the
fiber to the ready queue; EXIT drops it. Fiber objects remain caller-owned in
this ticket and may be destroyed only after the loop has returned to the
scheduler stack. NONE reaching dispatch is a fatal internal error. The loop
returns to the caller when the ready queue empties.

Live-fiber accounting was questioned out of this ticket (2026-08-30): with
no parking, it is a second source of truth over the queue, bought to enable
an error check unreachable by construction — the ring_entries sin again.
UC-010's parking tracks in-flight operations, which it needs for ring
accounting anyway; "live" is then the derivable sum ready + in_flight, not
a stored primitive.

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
every turn. Both exit, the loop returns with the ready queue empty, and their owners
destroy them from the scheduler stack. The former `no scheduler symbol` test is
retired by this ticket.

2026-08-30: done on aarch64 — the probe's two fibers produce exactly
`A0B0A1B1A2B2`, each turn checking its own thread-local value and identity;
the loop returns with the queue empty and the owners destroy from the
scheduler stack (container 0, VM `exitcode=0x00000000`). The surface is
become/enqueue/run over an intrusive FIFO; dispatch reuses fiber_resume;
live-fiber accounting and the entries parameter were questioned out (notes
in spec and findings.md), double-enqueue stays an accepted unchecked
contract until made illegal by construction. The no-scheduler-symbol test
is retired. x86-64 is compile-and-link per UC-012.
