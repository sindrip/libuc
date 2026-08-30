---
id: UC-007
title: Suspend and resume a fiber
status: todo
depends: [UC-006]
---

## Goal

Turn the completed-only fiber into a resumable continuation without adding a
scheduler.

## Spec

Replace the one-shot run contract with explicit resume and suspend operations.
Before every resume, the caller supplies the context to which the fiber will
suspend. A fiber may yield and later resume repeatedly; returning from its entry
function publishes EXIT and suspends for the final time. Resuming after EXIT is
invalid, and destruction remains legal only after control is back on the
caller's stack.

The fiber reports why it suspended through a tagged request with exactly the
kinds this ticket serves: NONE, YIELD, and EXIT. NONE is installed before every
resume and reaching the caller with it is a broken control transfer. A private
fiber-yield operation finds the running fiber through the TCB word from UC-006,
writes YIELD, and switches to its suspension target. There is no ready queue,
queue policy, I/O request, spawn operation, or public `<threads.h>` surface.

The context switch continues to carry the thread pointer. Suspending restores
the resumer's thread-local context; resuming reinstalls the fiber's block. The
bootstrap context has no block and must not touch `_Thread_local` state.

## Files

- `src/fiber/`
- `src/arch/*/fiber_arch.*`
- `test/fiber.c`

## Acceptance

On both architectures, one bootstrap context resumes the same fiber at least
three times, observing `bootstrap -> fiber -> bootstrap` on every turn. The
fiber's stack address, callee-saved registers, `_Thread_local` value, and current
fiber identity survive every suspension. Entry return produces EXIT, a second
resume after EXIT is never attempted, and no scheduler symbol exists in
`libc.a`.
