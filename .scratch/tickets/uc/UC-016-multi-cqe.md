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
by `ready_count`/`parked_count`, and never double-enqueues. The operation
machine — FREE and ACTIVE today, STAGED returning with cancellation —
changes per CQE and per consumption, and lives in a per-scheduler table
of records addressed by the completion key.

The completion key is `{generation, slot, tag}` packed as
tag 4 | slot 16 | generation 44, tag zero invalid so a CQE_MIXED gap
filler can never decode into a live record. The slot indexes the table
(shared-nothing, no atomics), the generation detects a CQE aimed at a
recycled slot, and the tag separates the operation's own results from
CQEs about it: cancellation outcomes, linked timeouts, zero-copy
notifications, transport doorbells — each enumerator arriving with its
consumer. `../../bpf-loop.md` shares this encoding; the pack/unpack pair
is its documentation.

**One delivery path (decided, landed).** A single-shot is a stream of
length one. Every CQE lands as a `{res, cqe_flags}` delivery in its
record's four-slot ring; consumers pop deliveries; a record retires when
a terminal delivery — `F_MORE` clear — is consumed with the queue
drained. Release is at consume-time, never reap-time: the one lifetime
rule streams require, adopted by singles at measured zero cost (await-NOP
benchmark, consolidated vs direct: ~252 vs ~250 ns/op single, ~83 vs ~83
ns/op across 64 fibers). There is no operation-kind dispatch; the former
SINGLE/STREAM kind is deleted from the design. What distinguishes
zero-copy send is its tags, not a kind: the primary CQE carries `F_MORE`
(`out/src/io_uring/net.c:1598,1609-1611`) and buffers as a non-terminal
delivery; the later notification, addressed by `sqe->addr3`
(`out/src/io_uring/net.c:1398-1404`) as {same slot, same generation,
NOTIFICATION tag}, is the terminal event that lets the record retire. A
preparation failure before notification allocation is an ordinary
one-CQE terminal error.

**The consumer seam (decided, next).** Arm and consume ride the fiber
request protocol, symmetric with AWAIT: `STREAM_OPEN` carries the
multishot SQE, allocates a record, stages it, and returns the handle
without blocking; `STREAM_NEXT` pops a buffered delivery or parks the
fiber as the record's waiter. `next()` returns the delivery — res and
terminality in one value, so a consumer cannot take a final payload
without learning the stream ended. The transport costs one switch
round-trip per call; at the benchmark's ~80 ns/op switch-and-bookkeeping
floor under syscall-paced streams this is noise, and the transport hides
behind `__libuc_fiber_stream_*`, swappable if a measurement ever says
otherwise. The delivery struct is private machinery: public libc calls
fold it to one-result-per-call POSIX shape at their boundary exactly as
`errno_result.h` folds single-shot res today.

The wake rule is edge-triggered and the waiter field is its whole
implementation: reap's push wakes only a fiber parked on this record
(`waiter != nullptr`), popping the delivery into it in the same breath.
An owner that is ready, running, or parked on a different operation only
accumulates deliveries; it is never enqueued twice and never spuriously
woken. `waiter` is set by NEXT parking and cleared by the wake — for
singles it is set for the record's whole tenancy, which is the length-one
degenerate case of the same rule.

**Backpressure (decided, after the seam).** push_delivery's capacity
trap becomes cancel-and-rearm: a full queue submits `ASYNC_CANCEL` for
the stream — deliberately triggering what the kernel does spontaneously
when it cannot post (`io_req_post_cqe` failing ends a multishot,
`out/src/io_uring/poll.c:305`) — the consumer drains, observes the
terminal, and rearms. The CANCEL tag and STAGED state return here with
their first readers: a cancel request has its own result CQE, and
kernel-held versus never-submitted changes the cancel protocol. The two
CQEs of a cancelled operation have no assumed order; UC-017 owns the
rest of cancellation (fiber death, zombies, delayed reclamation). For
buffer-selecting opcodes the later provided-buffer pool replaces the
policy with credit, under the invariant that makes "naturally bounded"
precise: **pool size ≤ queue capacity**, so the kernel can never hold
more claimable buffers than the record can hold deliveries.

Reap validates ring-format flags first: on today's ring `F_SKIP` remains
a fatal configuration-drift signal, ignored-before-decoding only if
CQE_MIXED is ever enabled. The `F_MORE`/`F_NOTIF` trap in reap lifts
when the seam lands; terminal detection is per-delivery at consume time,
never per-CQE at reap time.

Rules that hold the seam: waking is a fiber-state transition, never a
per-CQE action. `parked_count` counts blocked fibers; records are
counted by their states. A fiber's stack reclaims only at zero owned
operations (scheduler.md's lifetime rule). Cancellation is cancel, reap
the terminal, then recycle — never immediate release. A stale handle
cannot reach a recycled record: the key's generation fails validation.

The surface is the bounded receiver `../../language.md` §6 already
names: multishot accept is a receiver of connections; `next()` consumes
a buffered result or parks. `__libuc_fiber_await` keeps its
exactly-one-CQE contract throughout as the length-one veneer.

Landed so far (a853417..5ceffa3): the packed key, the record and table,
park/reap speaking the key, the one-path delivery queue with release at
consume, and the await-NOP benchmark with its baseline. Remaining, in
order: the STREAM_OPEN/STREAM_NEXT requests with the multishot-poll
forcing probe (`IORING_POLL_ADD_MULTI`, uapi io_uring.h:371, over
UC-014's pipe — no socket dependency); cancel-and-rearm behind the
capacity trap with explicit stream cancel-and-drain; slot-recycling
acceptance for repeated streams. The zero-copy notification probe waits
for `SEND_ZC`, which whichever of the network tickets lands next brings.

## Acceptance

- Two `F_MORE` CQEs reaped before the consumer runs: one ready-queue
  insertion, two results delivered in order.
- A CQE arrives while the owner is ready: buffered, no duplicate enqueue.
- The owner is parked on a different operation: no wake.
- A terminal delivery may carry a final result and retires its operation
  exactly once, with no counter underflow.
- Delivery-queue capacity exhausted: cancel-and-rearm observed, not a
  drop.
- A stream cancelled and drained through its terminal CQE; its slot
  recycles, a repeated stream reuses slots without table growth, and a
  stale generation aimed at a recycled slot fails validation.
- The await-NOP benchmark holds its baseline within noise after each
  slice (~250 ns/op single, ~83 ns/op crowd on the recorded setup).
- Zero-copy notification routing (both tags on one record) is the
  network ticket's probe, once `SEND_ZC` exists to force it.

Both architectures build clean; cancellation on fiber death and
end-of-life reclamation are UC-017's acceptance.
