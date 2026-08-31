---
id: UC-024
title: Thread exit, join, and detach
status: todo
depends: []
---

## Goal

Give `<threads.h>` a way to end a thread and reclaim it. `thrd_create` is
public with no `thrd_join`, `thrd_detach`, or `thrd_exit`, so a program that
spawns threads cannot release their mappings and the probe reaches for the
private `__libuc_fiber_destroy` to compensate.

## Spec

A fiber's record lives inside its own stack mapping, so exiting cannot free
it: the return value would go with the memory holding it. An exit therefore
leaves a zombie — mapping retained, result readable — until a join takes it.
Joinable is the default, as C11 requires.

`thrd_join(thr, res)` blocks until the target exits, takes its result, and
releases the record. If the target has already exited, the same happens
without blocking. `thrd_detach(thr)` gives up the right to join, so the
scheduler destroys the target at exit instead of keeping it. `thrd_exit(res)`
ends the calling thread from any depth with that result, exactly as returning
it from the entry does.

Ownership: the wait is an edge between two fibers, so the scheduler owns it
whole — neither end records the other. A per-scheduler table of
`{request, target}` is scanned at join and at exit; the request is how the
answer travels back, matching the one-shot completion path.

Waiting on a fiber is not waiting on the ring. `parked_count` decides whether
the reactor blocks in the kernel, and a join-blocked fiber has no completion
coming, so it needs a separate count; otherwise the reactor waits for a CQE
nobody will post. Nothing runnable, nothing in flight, and fibers still
blocked is a join cycle — a program bug, and a trap.

**What returning from root `main` means.** Startup today runs the loop until
the whole scheduler drains (`src/start.c`), so a `main` that returns while
children run waits for them. C11 says returning from `main` is `exit`, which
does not wait for other threads. This ticket must pick one and record it:
today's drain-everything, or `exit` semantics where the runtime stops when
`main` returns and unjoined threads are abandoned. The choice decides what a
resident PID 1 does with stragglers, so it interacts with invariant 6.

**Interaction with UC-017.** That ticket makes an *iterator owner* that exits
without cleanup into a zombie: cancel its operations, drain their terminal
events, then reclaim. This ticket is the thread half of the same lifetime —
the fiber's own record and result — and lands first because it needs no
operation records. When UC-017 arrives, exit-time reclamation must satisfy
both: a fiber is reclaimable only when it has been joined or detached *and*
owns no live operations.

## Files

- `include/threads.h`
- `src/threads/`
- `src/fiber/`
- `src/scheduler/`
- `test/threads.c`

## Acceptance

- A thread that exits before its join is joined without blocking, and one
  joined before it exits blocks until it finishes; both yield its result.
- A detached thread's mapping is released at exit with no join.
- `thrd_exit` from nested calls ends the thread with that result.
- Joining a thread that another fiber already joins traps.
- A cycle of joins with nothing runnable and nothing in flight traps rather
  than hanging.
- The probes stop building a nested scheduler: `thrd_create` plus `thrd_join`
  replaces `become`/`enqueue`/`run` everywhere except the probes that test the
  fiber primitive or the scheduler itself.
- Both architectures build and test cleanly.
