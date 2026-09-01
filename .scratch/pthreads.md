# pthreads as schedulers

Status: **proposal, 2026-09-01.** A design conversation record, not a
commitment. Nothing here is scheduled; the plan's "More schedulers" section
gates all of it on a concrete consumer.

## The split

`pthread_create` creates a scheduler: a `clone`d task that becomes a
scheduler, owns its own ring, and runs the start routine as its root fiber.
`thrd_create` stays what it is — a fiber on the caller's scheduler.

POSIX threads are the OS's threads; C11 threads are the runtime's. Both
tiers keep standard names, and POSIX already carries the placement
vocabulary C11 lacks: `pthread_attr_t` has stack size and affinity, and
contention scope (`PTHREAD_SCOPE_SYSTEM` versus `PTHREAD_SCOPE_PROCESS`) is
the historical name for exactly this kernel-scheduled/library-scheduled
distinction. Ported code that reaches for pthreads wants parallelism and
preemptive fairness between workers, and gets precisely that; `thrd_create`
stays the cheap cooperative primitive within a shard.

## Every mechanism stays on the ring

The pinned kernel already carries everything this needs
(`out/src/include/uapi/linux/io_uring.h:296,306-309`):

- **join**: a fiber awaits `IORING_OP_FUTEX_WAIT` on the child's
  `CLONE_CHILD_CLEARTID` word — glibc's join, except the wait suspends a
  fiber instead of a task.
- **mutexes and condvars**: futex wait/wake over the ring, with a
  same-scheduler fast path. This serves both `pthread_mutex_t` and `mtx_t`,
  since C11 does not know the tiers and an `mtx_t` may cross them.
- **doorbell**: `IORING_OP_MSG_RING` posts a completion into a sibling
  scheduler's ring, so waking another shard rides the same reap path as any
  CQE.
- **creation and placement**: `clone` and `sched_setaffinity` are already on
  the permitted direct-syscall list (AGENTS invariant 1).

Invariant 3 survives untouched: fibers never migrate, per-scheduler state
needs no atomics, and cross-scheduler synchronization exists only inside
futex-backed primitives the program explicitly invokes.

## Exit semantics: split lifetimes (decided in UC-029)

`start_routine` returning ends the root fiber; `pthread_join` returns
then; the scheduler drains its remaining fibers separately. This is
deliberately not the process rule from UC-024: a shard's children finish,
and the joiner is not held hostage while they do. The cost is visible
ordering — a shard can still be producing side effects after its join
returned. An earlier draft of this file applied UC-024's stop-the-world
rule fractally; UC-029 supersedes it.

## Costs

- The POSIX surface is a mountain. Mutexes and condvars must work at first
  landing because ported code leans on them; cancellation, robust mutexes,
  and fork interaction are subset or refused, and each refusal is a sharp
  edge for porters.
- A ring per pthread. Fine at the tens of threads real pools use; a
  pthread-per-connection design spawns a thousand reactors. A documented
  weight class, not a hidden one.
- `mtx_t` blurs. A mutex shared across shards needs the futex path, so
  "C11 threads are always cheap" holds only within a scheduler.

## Open questions

- `thrd_create` on any pthread spawns onto that pthread's scheduler — the
  caller's, as today. Confirm there is no other placement.
- A `thrd_t` never crosses a pthread boundary: handles stay home, messages
  travel. The join slot design (UC-024) depends on one scheduler owning both
  ends of the edge.
- First landing scope is fixed by UC-029: create, join, detach, self,
  attr, mutex (recursive included, for sqlite), cond — nothing else.
- Whether `transport.md`'s message delivery becomes a library over
  `MSG_RING` or stays a private runtime channel.

## Origin

A three-way benchmark (2026-09-01, `/tmp` scratch, epoll-enabled kernel
variant, 64 connections, closed-loop ping-pong, single HVF vCPU, release
builds) measured a raw-syscall epoll state machine, the fiber runtime, and
a hand-batched ring-native state machine on the same pinned kernel:

| 13 B response | ns/op | req/s |
|---|---|---|
| epoll, raw syscalls | 2250–2266 | 441–444k |
| libuc fibers | 2392–2399 | 417–418k |
| ring-native batched loop | 2506–2573 | 389–399k |

The fiber runtime beat the hand-written ring loop, so the abstraction is
effectively free; the residual gap to epoll is single-shot ring economics,
recoverable by UC-020/021/022's multishot and provided-buffer work. The
blocking-code programming model (a 12-line handler versus ~170 lines of
state machine) is the product this proposal extends to parallel shards.
