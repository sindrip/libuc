# RT-009 — Milestone 2: single-core echo server

- **id**: RT-009
- **status**: done
- **depends**: RT-006

## Goal

An echo *server*, not one echoed connection: accept in a loop, serve each
connection on its own fiber, concurrently, on one core and one ring.

## Spec

Three things stood between RT-006's `echo_once` and a server, and only one of
them was about io_uring.

**A fiber must be able to spawn a fiber.** A fiber has no route to its
scheduler — `owner` is null while it runs, there is no `rt_scheduler_current()`,
and `.scratch/scheduler.md` forbids fiber context from mutating scheduler state
regardless. So spawn is a request, `RT_REQUEST_SPAWN`, carrying `fn` and `arg`
and serviced re-entrantly in `rt_scheduler_resume` exactly as WAKE is: the
spawner keeps the CPU and is never re-queued. It joins WAKE as a **serviced**
request, and dispatch panics if one reaches it.

`-Wswitch -Werror` did its job: adding the kind failed the build until dispatch
handled it.

**Connection fibers must come from somewhere, and there is no allocator.** The
program hands the scheduler a fixed array of fibers (`rt_scheduler_provide`)
and the scheduler cycles them: pop from `idle` on spawn, push back on EXIT.
Each one's stack is `mmap`ed once and reused across lives, so a connection
costs no syscall for memory.

This changed `rt_scheduler_spawn`'s signature — it no longer takes the fiber to
start, it takes the work — and gave it a return value: `-EAGAIN` when the pool
is dry. That is the server's backpressure, and the acceptor must close the
connection it cannot serve.

Reclaiming a fiber at EXIT is safe today without any new rule: a fiber cannot be
running while it owes a completion, so `owner == nullptr` holds at EXIT by
construction, which is exactly the lifetime rule `.scratch/scheduler.md` states.

**`rt_fiber_create` split** into `rt_fiber_stack_alloc` (mmap, once) and
`rt_fiber_start` (context init, per life), since a recycled fiber must keep its
stack and reset everything else. `rt_fiber_create` remains as the composition of
the two, for the RT-004 driver that resumes a fiber by hand.

Dropped in passing: `struct rt_fiber`'s `fn` and `arg` fields, written by create
and read by nothing — the same class as the state field deleted in RT-006.

## Files

```
src/fiber.h        RT_REQUEST_SPAWN, struct rt_fiber_spawn, the split create
src/fiber.c        rt_fiber_stack_alloc / rt_fiber_start / rt_fiber_spawn
src/scheduler.h    the idle queue; spawn takes work and returns int
src/scheduler.c    provide, pool-backed spawn, SPAWN serviced in resume
src/main.c         accept_fiber, conn_fiber, the fiber array
```

## Acceptance

Boot, then drive it from the host through the forwarded port. 32 fibers, one
held by the acceptor, so 31 connections can be served at once.

1. **Concurrency.** Two clients, the first holding its connection open across
   the second's whole life:

   ```
   echo: listening
   echo: open 1
   echo: open 2
   echo: done 2
   echo: done 1
   ```

   `done 2` strictly inside `open 1 .. done 1` is the check. Sequential
   handling would print `done 1` before `open 2`.

2. **Fibers recycle.** 100 sequential connections, all echoed, `open`/`done`
   counts equal, no growth in anything.

3. **Exhaustion is reported, not fatal.** 40 connections held open at once:
   exactly 31 echo, exactly 9 print `echo: no free fiber` and are closed by the
   acceptor. The server stays up.

4. **The pool recovers.** After all 40 close, 35 sequential connections all
   echo.

5. **No regressions.** With `verbose = true`:
   `1A2B3C`, `ops 65 reg 38 feat 262143`, `setup rejects bogus flags: 22`,
   `nop ok`, `park/wake ok`, `hello a`, `hello b`.

All five measured on QEMU + hvf, 1 vCPU. 221 connections across the run, 221
opens, 221 dones, zero errors, no panic.

## Notes

**The provided-buffer-ring and multishot decision is in `plan.md`, not here.**
Short version: it is three separable decisions, multishot recv requires provided
buffers, and multishot is the operation-record rewrite rather than a flag. All
three deferred; this milestone is single-shot throughout.

**One harness artifact, so it is not mistaken for a runtime limit later.** Forty
*simultaneous* `connect()` calls lose roughly a quarter to connect timeouts —
QEMU's slirp under a thundering herd, since the guest's console shows no accept
for them at all. Staggering the connects by 50 ms establishes all 40 and puts
the failure exactly where it belongs, on the pool. Any future concurrency
measurement on this machine has to stagger, or it is measuring slirp.

**The yielder stays.** `yield_fiber` spins on `rt_fiber_yield` until the first
connection arrives. It exists to hold `ready.count` nonzero while the acceptor
blocks on ACCEPT, which is the exact shape of the DEFER_TASKRUN liveness bug
fixed in `93881af` — under the old condition the accept CQE was never reaped.
It is the only regression check for that fix.

**What the server does not do.** No timeouts, no graceful shutdown, no limit on
how long a connection may hold a fiber. A slow client holds one indefinitely,
which is a denial of service against the pool and needs cancellation — which
needs the operation records — to fix.

That is a gap in the runtime, not in the fixed array. An allocator would move
the limit from 32 to a memory bound and turn a clean `-EAGAIN` into an OOM,
which for PID 1 is the worse failure; what is actually missing either way is a
way to stop a fiber blocked in `rt_recv`. The array's size and the fact that
the *program* supplies it are the temporary parts — sizing belongs to the
runtime, from a workload statement, as `.scratch/scheduler.md` argues for
`ring_entries`.
