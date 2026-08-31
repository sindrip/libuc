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

**The consumer seam (built once, rejected 2026-09-01; open).** The shape
below was implemented end to end, passed every probe, and was backed
out: three fiber requests, a handle type and a cancel path is a lot of
surface for one feature, and unifying `await` onto it cost every
single-shot ~20% (crowd-64 ~83 to ~100 ns/op) to buy uniformity only
streams use. What replaces it is undecided; the record below is what was
tried, not what is settled. Arm and consume rode the fiber request
protocol, symmetric with AWAIT: `STREAM_OPEN` carries the
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

**Record lifetime (decided 2026-09-01).** Four rules, of which this
ticket carries three:

1. A record belongs to the fiber that opened it, and ownership is
   **transferable, one owner at a time** — handing a stream to a worker
   fiber is a legitimate move, consuming from two fibers at once is
   not. The handle is a one-field struct rather than a bare `uint64_t`,
   so it cannot be conflated with a result or silently arithmetic'd;
   C cannot make the move exclusive the way an owned Rust value would,
   which is why the rule is stated here.
2. The lifetime ends when the owner consumes the terminal delivery.
   Retirement stays a consequence of consumption, which is what makes
   the drain loop self-cleaning.
3. If the owner dies first the scheduler inherits the record — cancel,
   drain, retire. That is UC-017's subject. **Until it exists, this case
   traps**: a fiber that exits owning live records has leaked a
   kernel-held request, and a loud failure beats a silent leak.
4. The scheduler outlives every record: the loop may not return while
   records live. Today that is the same trap; UC-017 turns it into the
   drain.

A stale handle — one whose record retired, or whose slot has been
recycled under a new generation — is answered differently by the two
consuming operations, and the asymmetry is deliberate. `NEXT` traps:
consuming past a terminal delivery means the caller ignored the
terminality it was handed, which is a program bug, and the whole point
of returning terminality in the delivery is that it cannot be missed.
`CANCEL` is a no-op: cancel-if-unsure is a legitimate idiom, the
operation is advisory in the first place — the kernel may answer
`-ENOENT` for an already-finished target — and a stream that ended on
its own between the decision to cancel and the call is exactly the race
the idiom exists to tolerate.

Rule 3's eventual implementation needs "every record this fiber owns",
which is either an intrusive owner list or a table scan at fiber exit.
The list costs nothing in record size — `next` is unused while ACTIVE
and can serve the owner list there exactly as it serves the free list
while FREE — but unlinking on retirement is O(records the fiber owns),
which is wrong for the receiver shape where one fiber owns thousands of
streams. A scan is O(table) per fiber exit instead. UC-017 chooses; the
single-link trick is recorded so the choice stays open.

An orphaned record is caught late and by the wrong trap today: with
other fibers still running, the loop continues, the kernel keeps posting
into a record nobody consumes, and the delivery queue fills before the
loop ever reaches its exit check. Better diagnosis is part of rule 3's
implementation.

**Completion capacity (revised 2026-09-01; supersedes the reservation
rule).** UC-013 parked at most `cq_entries` operations because a full CQ
was believed to drop wakes. It does not. The kernel allocates an
overflow entry and queues it on `cq_overflow_list`
(`out/src/io_uring/io_uring.c:635-658`), flushing it back into the CQ on
a later enter (`io_uring.c:1217-1218`). Completions are lost only when
that allocation itself fails, and the kernel reports exactly that: the
DROPPED bit turns the next enter into `-EBADR`
(`io_uring.c:1223-1224`). So the reservation bought nothing, and
multishot voided it anyway — one record may post arbitrarily many CQEs,
so no per-operation reservation can bound completions.

The contract with the kernel is therefore: **overflow is a tolerated,
lossless path; `-EBADR` from enter is the one fatal signal.** The
reservation trap goes; `live_count` and `parked_count` keep no capacity
role. Submission is gated on the ring holding a staged batch, not on
counting records — a `STREAM_OPEN` fiber stages an SQE and keeps
running, and a cancellation stages one against no new record at all, so
a record-count gate strands SQEs.

**Per-record capacity must be prevented, not handled.** Reap must drain
the CQ: declining to consume stalls every operation behind the head, and
deadlocks a fiber parked on a completion stuck behind the stall. With no
allocator, a full delivery queue has nowhere to put the CQE in hand.
Multishot therefore divides by where its bound comes from:

- **Metered.** Multishot recv requires provided buffers
  (`out/src/io_uring/net.c:845`), so the pool is the kernel's permission
  to complete: **pool size ≤ queue capacity** makes overflow impossible
  by construction. `IORING_OP_RECV` additionally accepts a byte budget
  in `sqe->optlen`, which ends the stream when spent
  (`net.c:850-853,895-905`). This is what "naturally bounded" means.
- **Unmetered.** Multishot poll and accept have no kernel-side bound —
  nothing to meter, no budget. They are offered under an explicit
  consumer contract, *drain every scheduler pass*.

Overflow is surfaced as a decision, not a death: the delivery push
returns whether it succeeded, and the reap loop owns what happens next.
Trapping stays the answer while no caller has a better one, but the
shape keeps the choice at the level that can see the whole ring rather
than burying it in a leaf helper — the same reason the kernel returns
false from `io_req_post_cqe` instead of dying when it cannot post.

Cancel-and-rearm is consequently **not** an overflow policy — cancelling
on a full queue is unsound, since the cancellation is asynchronous and
the kernel may post further CQEs before it lands, with nowhere to put
them. Cancellation is a consumer-initiated operation: `stream_cancel`
submits `ASYNC_CANCEL` under the target's slot and generation with the
CANCEL tag, and the record still retires through its own terminal
delivery. The two CQEs have no assumed order, so a cancel completion may
arrive after its target retired and the slot was recycled; CANCEL-tagged
CQEs are therefore recognized before lookup and ignored unconditionally.
UC-017 owns the rest of cancellation (fiber death, zombies, delayed
reclamation).

Reap validates ring-format flags first: on today's ring `F_SKIP` remains
a fatal configuration-drift signal, ignored-before-decoding only if
CQE_MIXED is ever enabled. The `F_MORE`/`F_NOTIF` trap in reap lifts
when the seam lands; terminal detection is per-delivery at consume time,
never per-CQE at reap time.

Rules that hold the seam: waking is a fiber-state transition, never a
per-CQE action. The *waits-on* relation is the single source of truth
for whether a fiber is parked — a record names its waiter, and a fiber
waits on at most one record — so a parked count is derived, never
maintained alongside it. `ready_count` bounds one sweep and `live_count`
detects records outliving the loop; neither bounds capacity. A
fiber's stack reclaims only at zero owned operations (scheduler.md's
lifetime rule). Cancellation is cancel, reap the terminal, then recycle
— never immediate release. A stale handle cannot reach a recycled
record: the key's generation fails validation.

The surface is the bounded receiver `../../language.md` §6 already
names: multishot accept is a receiver of connections; `next()` consumes
a buffered result or parks. `__libuc_fiber_await` keeps its
exactly-one-CQE contract throughout as the length-one veneer.

Landed so far (a853417..5ceffa3): the packed key, the record and table,
park/reap speaking the key, the one-path delivery queue with release at
consume, and the await-NOP benchmark with its baseline. The consumer
seam was built on top of that and backed out; the multishot-poll forcing
probe written for it (`IORING_POLL_ADD_MULTI`, uapi io_uring.h:371, over
UC-014's pipe — no socket dependency) proved the machinery works, and is
worth recovering whatever shape replaces the seam.

Whatever that shape is, these still hold: drop the reservation trap, gate submission
on the ring's staged batch, recognize `-EBADR` as the fatal
completion-loss signal, wrap the handle in its struct, derive parked
from the waits-on relation instead of counting, and return the overflow
decision from the delivery push. The zero-copy
notification probe waits for `SEND_ZC`, which whichever of the network
tickets lands next brings.

## Acceptance

- Two `F_MORE` CQEs reaped before the consumer runs: one ready-queue
  insertion, two results delivered in order.
- A CQE arrives while the owner is ready: buffered, no duplicate enqueue.
- The owner is parked on a different operation: no wake.
- A terminal delivery may carry a final result and retires its operation
  exactly once, with no counter underflow.
- A metered stream cannot exhaust its delivery queue: a provided-buffer
  pool no larger than the queue leaves the kernel unable to post beyond
  it. An unmetered stream whose consumer stops draining traps.
- A stream cancelled and drained through its terminal CQE; its slot
  recycles, a repeated stream reuses slots without table growth, and a
  stale generation aimed at a recycled slot fails validation.
- The await-NOP benchmark holds its baseline within noise after each
  slice (~250 ns/op single, ~83 ns/op crowd on the recorded setup).
- Zero-copy notification routing (both tags on one record) is the
  network ticket's probe, once `SEND_ZC` exists to force it.

Both architectures build clean; cancellation on fiber death and
end-of-life reclamation are UC-017's acceptance.
