---
id: UC-002
title: Create and switch bare fibers
status: todo
depends: []
---

## Goal

Introduce the smallest useful fiber: an owned stack and saved register context.

## Spec

Add fiber creation, destruction, and a two-context switch. A new fiber starts
in a trampoline and returns to its caller when its entry function finishes.
There is no run queue, scheduler, I/O, spawn policy, or stack pool.

A fiber owns nothing thread-local: no block, no thread pointer. Its stack is
a plain read-write anonymous mapping; guard pages are deferred. The process
runs with no thread pointer installed, so the standing invariant — nothing
touches a `_Thread_local` — holds everywhere until UC-006 retires it.

## Files

- `src/fiber/`
- `src/arch/*/fiber_arch.*`

## Acceptance

A bootstrap context and one fiber switch `bootstrap -> fiber -> bootstrap`,
preserving callee-saved state and using the fiber's own stack on both
architectures.
