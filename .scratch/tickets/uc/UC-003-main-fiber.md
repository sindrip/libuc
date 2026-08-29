---
id: UC-003
title: Enter main on the root fiber
status: done
depends: [UC-002]
---

## Goal

Make the first C program context a fiber without introducing a scheduler.

## Spec

`_start` enters a bootstrap context on the kernel stack. Startup parses
`argc`/`argv`/`envp` and auxv, records the thread-local image, creates the
root fiber, and switches to it. The root fiber runs constructors and `main`.
When `main` returns, its status is transferred back to the bootstrap context
for `exit_group`.

The kernel stack is bootstrap storage only. It is not called a scheduler and
owns no queue, ring, arena, or pool. Thread-local state stays uninstalled;
constructors and `main` remain bound by the no-`_Thread_local` invariant.

## Files

- `src/start.c`
- `src/arch/*/start.c`
- `src/fiber/`
- `test/main.c`

## Acceptance

On both architectures, constructors and `main` run on the root fiber's
mapping, `main`'s return status reaches `exit_group`, and no scheduler symbol
exists in `libc.a`.
