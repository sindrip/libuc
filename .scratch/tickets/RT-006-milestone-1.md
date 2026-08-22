---
id: RT-006
title: Milestone 1 — one ring, one coroutine, one NOP
status: done
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

**Status: deferred back to notes (2026-08-22, same day).** These were briefly
promoted to milestone-1 requirements and partially built; that was drift.
The spec above already carried the right instruction for all of it — "note
it now; don't build it yet" — and milestone 1 builds the spec's shape:
pointer user_data, caller-owned tasks, a `result` field, nothing more. The
items below are migration notes for the eras that need them (multishot and
cancellation for inflight/F_MORE; the BPF loop for the offset encoding and
the header split; milestone 3 for the scheduler struct). The arguments
stand; the timing was wrong.

The governing idea remains worth keeping for later: **the userspace reap
loop is `loop_step`'s twin** — when those eras arrive, shape the loop so
the lowering is a port, not a redesign.

## Observed (2026-08-23, acceptance sweep)

- **The milestone, on the first boot the code ever took**: after the
  RT-005 chain,

      nop ok
      hello a
      hello b

  then silence with no panic — the last task's death dropped the scheduler
  back to rt_main's idle. Each hello written by the kernel through
  IORING_OP_WRITE; the per-task messages are the identity proof (RT-004's
  1A2B3C lesson): the right task resumed with the right argument, twice
  each, through two suspensions.
- **The raw_write-stub boot, the strong form of the through-the-ring
  proof**: with raw_write compiled as a trap and the pre-demo chain gated,
  the console was exactly

      hello a
      hello b

  Any code path touching the purity exception would have panicked; none
  did. This boot is also the spec's quiet console, now the default: the
  RT-003..005 chain sits behind a volatile `verbose` gate in rt_main,
  compiled in for AGENTS.md's re-run discipline, silent for the spec.
  The stub build's volatile bool summoned UBSan handler nine
  (load_invalid_value) via the lazy link-error mechanism.
- **./debug.sh**: a hardware breakpoint (software breakpoints predate the
  MMU; -H works from reset) at 0x210d84 — the instruction after
  bl rt_switch in rt_nop, which lldb symbolizes as
  `rt_nop + 92 [inlined] rt_suspend + 40 at sched.c:26` — hit on the first
  resume with sp = 0xffffb86f2f90 in the task's own mmap'd stack region
  and x19 = self = 0xffffcc687150 on the boot stack: two regions in one
  frame, the suspension real at register level. result == 0 is proven
  behaviorally: the task's own res != 0 branch never fires on any boot.
- **Batching observed structurally**: both tasks' SQEs go to the kernel in
  single enters per turn (the staged count is cached_sq_tail's lead over
  the published tail), per the spec's not-an-optimization note.
- **Post-acceptance short-submit correction**: a nonnegative `io_uring_enter`
  return is not enough. `io_submit_sqes` can stop after request-allocation
  failure or a bad SQE and return a positive prefix count
  (`io_uring.c:2046-2068`), after which `io_uring_enter` returns without
  waiting (`io_uring.c:2646-2650`). Because the full tail was already
  published, the old `cached_sq_tail - sq_tail` calculation then reported no
  newly staged work while the suffix remained behind `sq_head`, leaving its
  tasks blocked forever. Milestone 1 now requires `ret == staged` and panics
  otherwise. The recoverable design must count
  `cached_sq_tail - sq_head` and retry a partially consumed batch.
- **Reap-state tripwire**: before delivering a CQE, the scheduler requires its
  decoded task to be `RT_BLOCKED`. Under milestone 1's immediate-suspend,
  one-shot contract, any other state means a duplicate or stale completion;
  panicking prevents it from silently overwriting `result` or making an
  already-runnable task ready again. This does not identify the awaited
  operation and therefore does not replace the slab offsets, tags, generations
  and `inflight` accounting required by multishot and cancellation.
- RT-007's deferred criterion (the dump naming the running task) remains
  open — rt_current now exists and crash.c's e) seam can consume it in a
  follow-up; the task registry the walk's range-check wants is still a
  deferred-notes item.

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
