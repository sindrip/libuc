---
id: UC-019
title: Detect completion loss
status: todo
depends: []
---

## Goal

Make the ring report the only unrecoverable CQ condition directly: a CQE that
the kernel could not retain. Do not change the current exactly-one-CQE reactor
or introduce operation records.

## Spec

A full CQ is normally lossless. Linux allocates an overflow entry and appends
it to `cq_overflow_list` (`out/src/io_uring/io_uring.c:635-658`), then moves
overflow entries back into the mapped CQ on a later enter
(`out/src/io_uring/wait.c:206-207`). If allocation fails, the kernel increments
the mapped `cq_overflow` word and sets `IO_CHECK_CQ_DROPPED_BIT`
(`out/src/io_uring/io_uring.c:640-650`).

`-EBADR` is a fatal signal when an enter returns it, but it is not a sufficient
detector. The ordinary wait path returns early whenever the visible CQ already
satisfies `min_complete` — always true for zero — before checking DROPPED
(`out/src/io_uring/wait.c:189-213,292-300`). An enter that successfully submits
SQEs also returns its positive submission count instead of a completion-side
error (`out/src/io_uring/io_uring.c:2640-2662,2689-2699`).

Map `params.cq_off.overflow` as an atomic kernel-owned word in
`struct __libuc_ring`. It must be zero after setup. Check it after every enter
and before treating a drained reactor pass as lossless; any nonzero value is a
fatal invariant failure. Continue treating a returned `-EBADR` as fatal too.
The counter is evidence that identity or wake information has already been
lost, so retry and recovery are impossible.

The existing admission rule remains valid for the current reactor: at most
`cq_entries` parked operations, each promising exactly one CQE, proves the CQ
never needs overflow storage. This ticket corrects its explanation rather than
removing it. A later producer that can emit several CQEs needs its own bound and
does not inherit this proof.

## Acceptance

- The ring records the exact `cq_off.overflow` address returned by setup and
  observes zero through a NOP submission and reap.
- A small ring-level fixture points the loss check at a synthetic nonzero atomic
  word and observes the fatal decision without requiring kernel OOM.
- The scheduler's existing single-CQE probes retain their behavior and the
  admission trap remains.
- Both architectures build and test cleanly.
