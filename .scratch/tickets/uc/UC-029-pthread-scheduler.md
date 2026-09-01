---
id: UC-029
title: pthread_create spawns a scheduler
status: todo
depends: [UC-025, UC-026, UC-027, UC-028]
---

## Goal

`pthread_t` is the root fiber of a spawned scheduler; `thrd_t` stays a
fiber on the calling one.

## Spec

Mutexes and condvars are futex words over UC-027; recursive mutexes are
required for sqlite.

Lifetimes split: `start_routine` returning ends the root fiber, `join`
returns then, and the scheduler drains separately. This supersedes the
fractal stop-the-world draft in `../pthreads.md`, and is deliberately not
the process rule: a shard's remaining fibers finish after the joiner has
its answer.

The public header states plainly that `thrd_create` fan-out gives
concurrency and no speedup. `pthread_create` is documented as not cheap —
a task, a ring, a reactor. Signals can no longer be deferred.

## Files

- `include/pthread.h`
- `src/pthread/`
- `test/`

## Acceptance

- Two pthreads run truly in parallel under `ACCEL=tcg SMP=4`.
- A mutex contended across schedulers suspends the loser as a fiber; its
  scheduler keeps serving.
- `pthread_join` returns at root-fiber exit while a remaining green fiber
  on that shard still runs to completion afterward.
- Both architectures build and test cleanly.
