---
id: UC-016
title: Multi-CQE operations
status: todo
depends: [UC-013, UC-014]
---

## Goal

Support operations whose one SQE produces many CQEs — multishot accept and
recv, zero-copy send notifications — without corrupting the reactor's
single-shot routing.

## Spec

Two independent state machines, never conflated. The fiber machine —
ready, running, parked, dead — changes once per block or wake, is counted
by `ready`/`parked`, and never double-enqueues. The operation machine —
staged, active, terminal, free — changes per CQE and is counted by
`active_ops`, decremented only at the operation's terminal event.

The completion key stops being a fiber. `user_data` encodes
{generation, slot, tag}: the slot indexes a per-scheduler operation slab
(shared-nothing, no atomics), the generation detects a CQE aimed at a
recycled slot, and the tag separates ordinary operations, cancellation,
linked timeouts, notifications, and transport. `.scratch/scheduler.md`
("The completion key, later") already commits to exactly this — a
scheduler-owned, generation-bearing record living until the final CQE.
The operation slab is its own identity space: a record may reference a
task-header slot, never be one, since a fiber owns several operations the
moment cancellation or concurrency exists. `.scratch/bpf-loop.md`'s
offset-plus-tag encoding then addresses this slab — one encoding,
BPF-bounds-checkable, decided once.

The record: owner, generation, kind, state, and a bounded delivery queue
with an explicit capacity and an overflow policy — one `{res, cqe_flags}`
slot is not enough, since a stream posts several CQEs before its consumer
runs. The wake rule is edge-triggered: one ready-queue insertion on the
queue's empty-to-nonempty transition, never one per CQE.

Reap decodes identity first (`F_SKIP` gap fillers carry `user_data = 0`
and are ignored before decoding), then dispatches on kind, and terminal
detection is kind-specific. SINGLE: the one CQE finishes it. STREAM:
`F_MORE` clear finishes it — delivery never retires a live stream
(uapi cqe->flags block). ZC_SEND is a two-phase protocol of its own: the
primary completion carries `F_MORE` (`net.c:1598`) and the later
notification carries `F_NOTIF` under a possibly different `user_data`
from `sqe->addr3` (`net.c:1398`), so the notification is its own record,
not the primary's second CQE. CANCEL and LINK_TIMEOUT: bookkeeping, no
wake.

Rules that hold the seam: waking is a fiber-state transition, never a
per-CQE action — an already-ready owner buffers without a second enqueue.
`parked` counts blocked fibers, `active_ops` counts kernel-held records.
A fiber's stack reclaims only at zero owned operations (scheduler.md's
lifetime rule). Cancellation is cancel, reap the terminal CQE, then
recycle the slot — never immediate release.

The surface is the bounded receiver `.scratch/language.md` §6 already
names: multishot accept is a receiver of connections, next() consumes a
buffered result or parks. Capacity exhaustion takes an explicit
per-opcode policy — cancel and rearm, or a naturally bounded
provided-buffer pool.

Sequencing: UC-014 lands on single-shot operations only, and
`__libuc_fiber_await` keeps its exactly-one-CQE contract throughout. This
ticket carries operation identity, bounded stream delivery, and the
multishot forcing probe; cancellation, the zombie lifetime, and
generation-safe slot recycling are UC-017.

## Acceptance

- Two `F_MORE` CQEs reaped before the consumer runs: one ready-queue
  insertion, two results delivered in order.
- A CQE arrives while the owner is ready: buffered, no duplicate enqueue.
- The owner is parked on a different operation: no wake.
- A terminal CQE retires its operation exactly once, no counter underflow.
- Delivery-queue capacity exhausted: the chosen backpressure policy is
  observed, not a drop.
- Zero-copy notification routing tested separately from multishot routing.

Both architectures build clean; cancellation and recycling cases are
UC-017's acceptance.
