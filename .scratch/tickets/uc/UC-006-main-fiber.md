---
id: UC-006
title: Enter main on the root fiber
status: todo
depends: [UC-005]
---

## Goal

Make the first C program context a fiber without introducing a scheduler.

## Spec

`_start` enters a bootstrap context on the kernel stack. Startup parses
`argc`/`argv`/`envp` and auxv, records the thread-local image, creates the root
fiber, and switches to it. The root fiber runs constructors and `main`. When
`main` returns, its status is transferred back to the bootstrap context for
`exit_group`.

The kernel stack is bootstrap storage only. It is not called a scheduler and
owns no queue, ring, arena, or pool.

## Files

- `src/start.c`
- `src/arch/*/start.S`
- `src/fiber/`
- `test/main.c`

## Acceptance

On both architectures, constructors and `main` see initialized independent
thread-local state, `main` executes on the root fiber's mapping, its return
status reaches `exit_group`, and no scheduler symbol exists in `libc.a`.
