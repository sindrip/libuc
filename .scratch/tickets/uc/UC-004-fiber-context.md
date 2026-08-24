---
id: UC-004
title: Create and switch bare fibers
status: todo
depends: [UC-002]
---

## Goal

Introduce the smallest useful fiber: an owned stack and saved register context.

## Spec

Add fiber creation, destruction, and a two-context switch. A new fiber starts
in a trampoline and returns to its caller when its entry function finishes.
There is no run queue, scheduler, I/O, spawn policy, or stack pool.

## Files

- `libuc/src/fiber/`
- `libuc/src/arch/*/fiber_arch.*`

## Acceptance

A bootstrap context and one fiber switch `bootstrap -> fiber -> bootstrap`,
preserving callee-saved state and using the fiber's own stack on both
architectures.
