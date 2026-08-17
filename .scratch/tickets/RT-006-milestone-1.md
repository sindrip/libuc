---
id: RT-006
title: Milestone 1 — one ring, one coroutine, one NOP
status: todo
depends: [RT-004, RT-005]
---

## Goal

Join the two halves. A task suspends on a ring operation, the scheduler reaps
its completion and resumes it, and the task writes to the console **through the
ring**. This is the first moment the thing is a runtime rather than a pile of
parts.

## Spec

### The suspend protocol

```c
long rt_nop(void) {
    struct rt_task *self = rt_current;
    struct io_uring_sqe *sqe = rt_sqe_get();
    rt_prep_nop(sqe);
    sqe->user_data = (unsigned long)self;   // the task IS the completion key
    self->state = RT_BLOCKED;
    rt_switch(&self->ctx, &rt_sched_ctx);   // scheduler runs; returns here later
    return self->result;                     // filled in by the reaper
}
```

`user_data` carrying the task pointer is the whole dispatch mechanism: the
scheduler casts it back, stores `cqe->res` into `task->result`, marks the task
`RT_READY`, and resumes it. No completion table, no lookup.

Tag the low bits later if non-task completions appear (the watchdog timeout in
milestone 3 will need this). Note it now; don't build it yet.

### The scheduler loop

```
for (;;) {
    run every RT_READY task to its next suspension point
    if (nothing in flight && nothing ready) idle
    io_uring_enter(fd, to_submit, 1, IORING_ENTER_GETEVENTS, NULL, 0)
    for each CQE: t = (task *)cqe->user_data; t->result = cqe->res; t->state = RT_READY
    advance CQ head with a release store
}
```

Submissions are **batched** and issued once per turn, not per operation. This is
not an optimisation — it is what keeps the door open to `SQ_REWIND` and to the
BPF in-kernel loop later, both of which assume the loop owns submission timing.

`DEFER_TASKRUN` means completion work only runs inside this `io_uring_enter`
with `GETEVENTS` set. That is the desired property: completions land at exactly
one point in the loop, and nowhere else.

### The demonstration

Task body:

1. `raw_write` is now forbidden here — use the ring.
2. `rt_nop()`, assert `result == 0`.
3. `rt_write(1, "hello\n", 6)` via `IORING_OP_WRITE`, suspending the same way.
4. Return; the trampoline marks the task dead and switches to the scheduler.

`rt_main` must never return: after the last task dies, fall into an idle loop.

## Files

- `src/sched.c`, `src/sched.h`
- `src/task.c` — add `rt_current`, blocked/ready transitions
- `src/ring.c` — `rt_sqe_get`, `rt_prep_nop`, `rt_prep_write`
- `src/main.c`

## Acceptance

- Console shows `hello` and nothing else on the happy path.
- No kernel panic — PID 1 stayed alive.
- The `hello` was written by `IORING_OP_WRITE`, not `raw_write`. Verify by
  temporarily stubbing `raw_write` to abort; the run must still print `hello`.
- Under `./debug.sh`, a breakpoint after `rt_switch` in `rt_nop` shows `sp` back
  inside the task stack and `self->result == 0`.
- Two concurrent tasks each doing NOP-then-write interleave correctly and both
  complete.

## Notes

The two-task variant in the last acceptance item is worth doing even though the
milestone says one: a single task cannot distinguish "the scheduler resumes the
right task" from "the scheduler resumes the only task".

If `hello` appears but the process then panics, the bug is the trampoline
returning off the end of a task stack rather than switching back — RT-004's
trampoline contract.
