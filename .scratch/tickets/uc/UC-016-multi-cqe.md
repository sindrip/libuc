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
`live_ops` from allocation until terminal processing and slot release. Its
state distinguishes SQEs not yet submitted from requests held by the kernel;
the count keeps the scheduler alive in either case.

The completion key stops being a fiber. `user_data` encodes
{generation, slot, tag}: the slot indexes a per-scheduler operation table
(shared-nothing, no atomics), the generation detects a CQE aimed at a
recycled slot, and the tag separates ordinary operations, cancellation,
linked timeouts, notifications, and transport. `../../scheduler.md`
("Completion identity") records the same seam — a
scheduler-owned, generation-bearing record living until the final CQE.
The operation table is its own identity space: a record may reference a
fiber, never be embedded in one, since a fiber owns several operations the
moment cancellation or concurrency exists. `../../bpf-loop.md` uses this same
table — one encoding, BPF-bounds-checkable, decided once.

The record: owner, current waiter (if any), generation, kind, state, and a
bounded delivery queue
with an explicit capacity and an overflow policy — one `{res, cqe_flags}`
slot is not enough, since a stream posts several CQEs before its consumer
runs. The wake rule is edge-triggered: an empty-to-nonempty transition wakes
the owner only when that fiber is parked waiting on this operation. An owner
that is ready, running, or parked on a different operation only accumulates
delivery; it is never enqueued twice and never spuriously woken.

Reap validates ring-format flags first. `F_SKIP` gap fillers carry
`user_data = 0` and are ignored before identity decoding only when
`IORING_SETUP_CQE_MIXED` is enabled; on today's ring they remain a fatal
configuration-drift signal. Reap then decodes identity, dispatches on kind,
and performs kind-specific terminal detection. SINGLE: the one CQE finishes
it. STREAM: `F_MORE` clear means this CQE is the terminal event, not that it
has no payload; deliver its result according to the opcode and then finish the
operation. Delivery never retires a stream while `F_MORE` remains set
(`out/src/include/uapi/linux/io_uring.h:515-533`). ZC_SEND is a two-phase
protocol on one record once the kernel allocates its notification: the primary
completion carries `F_MORE` (`out/src/io_uring/net.c:1598,1609-1611`) and the
later notification carries `F_NOTIF`, addressed by `sqe->addr3`
(`out/src/io_uring/net.c:1398-1404`) as {same slot, same generation, NOTIF tag}
against the primary's RESULT tag. A preparation failure before notification
allocation is an ordinary one-CQE terminal error. A separate record for the
two-phase case would leave the primary's `F_MORE` half with no terminal
transition and leak `live_ops`; the record retires when both phases have
landed. CANCEL and LINK_TIMEOUT are bookkeeping, not automatic wakes.

Rules that hold the seam: waking is a fiber-state transition, never a
per-CQE action — an already-ready owner buffers without a second enqueue.
`parked` counts blocked fibers; `live_ops` counts allocated operation records.
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
ticket carries operation identity, bounded stream delivery, ordinary
terminal-slot recycling (a repeated stream must not exhaust the table),
explicit stream cancel-and-drain (the overflow policy depends on it),
and the multishot forcing probe — forced with multishot poll
(`IORING_POLL_ADD_MULTI`, uapi io_uring.h:371) over UC-014's pipe, so no
socket dependency. UC-017 owns what exit makes automatic: cancellation
on fiber death, zombies, and delayed reclamation. The zero-copy
notification probe waits for the ticket that brings sockets and
`SEND_ZC`; UC-014 has neither.

Decided 2026-08-31: `user_data` is tag in bits 0-3 (zero invalid, so a
gap filler's zero key never decodes), slot in bits 4-19, generation in
bits 20-63. The table is a constexpr 256 records per scheduler; exhaustion
traps until a measurement funds growth. The delivery queue is four inline
`{res, flags}` slots; capacity overflow cancels the stream at the kernel
and the consumer rearms after draining — the same rearm path a
kernel-ended stream already requires.

## Acceptance

- Two `F_MORE` CQEs reaped before the consumer runs: one ready-queue
  insertion, two results delivered in order.
- A CQE arrives while the owner is ready: buffered, no duplicate enqueue.
- The owner is parked on a different operation: no wake.
- A terminal CQE may carry a final delivered result and retires its operation
  exactly once, with no counter underflow.
- Delivery-queue capacity exhausted: the chosen backpressure policy is
  observed, not a drop.
- A stream cancelled and drained through its terminal CQE; its slot
  recycles, a repeated stream reuses slots without table growth, and a
  stale generation aimed at a recycled slot fails validation.
- Zero-copy notification routing (both tags on one record) is the network
  ticket's probe, once `SEND_ZC` exists to force it.

Both architectures build clean; cancellation and recycling cases are
UC-017's acceptance.
