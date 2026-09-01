---
id: UC-027
title: Cross-scheduler wait over ring futex operations
status: todo
depends: [UC-025]
---

## Goal

A contended wait is an ordinary await: `IORING_OP_FUTEX_WAIT`/`WAITV`/
`WAKE`, so the waiting fiber's scheduler keeps serving while it waits.

## Spec

Uncontended acquire is a compare-exchange with no SQE. Memory ordering
becomes load-bearing for the first time, with AArch64 as the real target.

The all-schedulers-waiting deadlock is not detected: a CQE is genuinely
outstanding, so it is indistinguishable from legitimate blocking.

## Files

- `src/uc/`
- `src/scheduler/`
- `test/`

## Acceptance

- A fiber on one scheduler wakes a waiter on another through the ring;
  the waiter's scheduler serves other fibers while it waits.
- The uncontended path issues no SQE.
- Both architectures build and test cleanly; behavioral acceptance under
  `ACCEL=tcg SMP=4`.
