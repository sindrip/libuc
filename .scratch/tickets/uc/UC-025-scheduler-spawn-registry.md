---
id: UC-025
title: Native scheduler spawn, identity, and registry
status: todo
depends: []
---

## Goal

Make a second scheduler possible. `clone3` a task; it calls `become` on
itself, creates and enqueues a root fiber, and enters its loop.

## Spec

`<uc/scheduler.h>` is the first public non-POSIX header, carrying what
`pthread_attr_t` cannot express: placement policy, ring parameters, eager
versus lazy ring creation. Installing it moves the implementation under
`src/uc/` per the placement rule.

Scheduler ids and a registry come due here, since a second scheduler can
now name a first; slot generations land with them because destruction and
reuse now exist.

Restates no-migration and adds no-work-stealing as an explicit non-goal.

## Files

- `include/uc/scheduler.h`
- `src/uc/scheduler/`
- `src/scheduler/`
- `test/`

## Acceptance

- A probe spawns a second scheduler, runs fibers on both, and every fiber
  is created, scheduled, and destroyed by its own scheduler.
- The registry resolves a scheduler id to a live scheduler and refuses a
  stale generation.
- Both architectures build and test cleanly; behavioral acceptance is the
  in-VM console check (`ACCEL=tcg SMP=4` for real parallelism).
