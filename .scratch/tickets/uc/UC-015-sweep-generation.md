---
id: UC-015
title: Bound the sweep to a generation
status: done
depends: [UC-013]
---

## Goal

Keep a persistent yielder from starving in-flight completions: UC-013's
sweep runs to empty, and a yielder re-enqueued at the tail keeps the sweep
alive forever — batched SQEs never submit, CQEs are never reaped.

## Spec

Sweep one generation: only the fibers already ready when the sweep began,
tracked by a `ready` count the enqueue and dequeue maintain (with its
sibling `parked`, the pair replaced the padding slot). At the boundary:
parked == 0 means pure yielders, no kernel entry; otherwise one enter per
generation —
`submit(0)` while the queue is non-empty, `submit(min_complete = 1)` once
it drains. Enter frequency stays a knob (every generation until a
measurement argues otherwise) — the family of eager-submit tuning UC-013
keeps out of scope. Landed 2026-08-30, pulled forward by review: under
`DEFER_TASKRUN` completions post only at enter, so the unbounded sweep
starved even already-submitted I/O, not just NOPs. Landing it moved
UC-013's acceptance order to `A0 B0 B1 A1 B2 A2` — the wake now lands one
generation after the park instead of after the queue empties.

## Acceptance

`test/sweep_generation.c`: fiber A parks on a NOP while fiber B yields
under a five-turn limit. B observes A's wake on exactly its third turn
(`turns_until_wake == 2`), the loop returns with nothing live or queued,
and both architectures build clean. In-VM exit status 0.
