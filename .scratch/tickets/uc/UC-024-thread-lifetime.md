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

Ownership: the wait is an edge between two fibers, and the scheduler owns it
whole. C11 admits at most one joiner (7.26.5.3 leaves a second join
undefined), so the edge is a single slot in the target's record —
scheduler-owned like the ready link. The suspended request parks in the
slot and is how the answer travels back, matching the one-shot completion
path; a second join finds the slot taken and traps.

Waiting on a fiber is not waiting on the ring. `parked_count` decides whether
the reactor blocks in the kernel, and a join-blocked fiber has no completion
coming, so it needs a separate count; otherwise the reactor waits for a CQE
nobody will post. Nothing runnable, nothing in flight, and fibers still
blocked is a join cycle — a program bug, and a trap.

**What returning from root `main` means — decided: C11 semantics.** Returning
from `main` is `exit` with that value (C11 5.1.2.2.3): the reactor stops when
the root fiber's entry returns, unjoined threads are abandoned, and
`exit_group` reclaims every mapping and in-flight operation with no cancels
and no teardown walk. `thrd_exit` from `main` ends only main's thread; the
program then terminates as if by `exit(EXIT_SUCCESS)` once the last thread
does (C11 7.26.5.6, the sentence DR 411 added), so today's drain loop
survives as that path, not as the return contract. The mechanism is a
process-exit request distinct from a thread's EXIT, raised by the root
trampoline after `main` returns; a future public `exit` raises the same
request from any fiber. Invariant 6 falls on the program: a resident PID 1
keeps itself alive by construction, and its `main` returning stays the
attempted-to-kill-init envelope.

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
- A `main` that returns while other threads run ends the program with its
  return value; the others never run again. A `main` that ends with
  `thrd_exit` leaves them running, and the program exits `EXIT_SUCCESS` after
  the last thread finishes.
- Joining a thread that another fiber already joins traps.
- A cycle of joins with nothing runnable and nothing in flight traps rather
  than hanging.
- The probes stop building a nested scheduler: `thrd_create` plus `thrd_join`
  replaces `become`/`enqueue`/`run` everywhere except the probes that test the
  fiber primitive or the scheduler itself.
- Both architectures build and test cleanly.
