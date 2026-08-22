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

Tag the low bits later if non-task completions appear (milestone 4's
`MSG_RING` doorbells will need this). Note it now; don't build it yet.

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

## Design amendments (2026-08-22, from language.md / bpf-loop.md)

Argued before implementation; the spec above predates these documents. The
governing idea: **the userspace reap loop is `loop_step`'s twin** — write it
as the reference implementation of the future in-kernel loop
(bpf-loop.md), and the lowering later becomes a port, not a redesign.

1. **`user_data` is a slab offset with tag bits, not a raw task pointer.**
   The spec's `(unsigned long)self` is replaced. bpf-loop.md's verdict —
   "adopt regardless of BPF" — has two justifications that apply today:
   an offset into a bounded slab is bounds-checkable at reap time (a debug
   assertion raw pointers cannot offer), and language.md's teardown law
   (nothing a pending SQE references is reclaimed before its CQE) needs the
   CQE→task mapping to stay decodable after task death. Scheduler tasks
   therefore live in a fixed slab; `user_data = byte_offset | tag`, tags in
   the low four bits (rt_task alignment ≥ 16 leaves them free). Only
   `TAG_OP` exists in milestone 1; the names `TAG_MSG`, `TAG_LTIMEOUT`,
   `TAG_CANCEL` are reserved per bpf-loop.md, not built.

2. **The task grows the kernel-plane header, shaped for the future split.**
   Four new fields as one contiguous block, in bpf-loop.md's order:
   `state, inflight, res, cqe_flags`. The block is what would migrate into
   `param_region` under the BPF loop; keeping its shape now is what makes
   the reap loop a twin. `inflight` also implements the teardown law: a
   dead task's slot recycles only at zero. State enum gains RT_BLOCKED
   (RT_READY stays pinned at 0 per task.h's rule); RT_ZOMBIE is reserved
   with the tags, not built.

3. **`IORING_CQE_F_MORE` handling is core, not future-proofing**: the
   inflight decrement is skipped while MORE is set. language.md made
   multishot the language's iterator protocol, so this branch underlies
   every future stream; it costs one condition in milestone 1, where no
   multishot op exists yet.

4. **Scheduler state is a struct, instantiated once.** language.md's boot
   contract: init creates exactly one scheduler and multi-core is library
   code — the runtime kernel is finished at single-core. `struct rt_sched`
   (ring, task slab, current) with a single static instance keeps that
   honest; milestone 3 multiplies instances, it does not restructure.

5. **RT-007's deferred acceptance lands here**: the dump names the running
   task and its stack range (crash.c's e) seam) once rt_current and the
   slab exist. The same registry is what later upgrades dump_frames from
   count-free validation to the kernel unwinder's range-check design —
   noted, not in this ticket's scope.
