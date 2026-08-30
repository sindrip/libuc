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

At this ticket boundary the kernel stack was bootstrap storage only: it was not
called a scheduler and owned no queue, ring, arena, or pool. UC-009 and UC-010
later promoted that same live context to scheduler zero's control stack.
Thread-local state stayed uninstalled here; constructors and `main` remained
bound by the no-`_Thread_local` invariant until UC-006.

## Files

- `src/start.c`
- `src/arch/*/start.c`
- `src/fiber/`
- `test/main.c`

## Acceptance

Acceptance at closure: on both architectures, constructors and `main` ran on
the root fiber's mapping, `main`'s return status reached `exit_group`, and no
scheduler symbol existed in `libc.a`. The last condition was deliberately
retired by UC-009.
