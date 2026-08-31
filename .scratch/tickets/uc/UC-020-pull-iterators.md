---
id: UC-020
title: Pull iterators over operation records
status: todo
depends: [UC-019]
---

## Goal

Add the common mechanism for a fiber-owned, fallible pull iterator whose source
may deliver several CQEs. Prove it with multishot poll without committing
accept, receive-buffer, or zero-copy-send policy.

## Spec

Operation records unify completion routing; typed iterators unify repeated
value delivery. Kernel multishot is an implementation strategy, not an API
contract. The common consumer protocol is:

```text
open
next -> item | end | error
destroy -> cancel, drain, reclaim
```

Install `<uc/iterator.h>` with only the shared result vocabulary:
`UC_ITER_ERROR = -1`, `UC_ITER_END = 0`, and `UC_ITER_ITEM = 1`. C has no
generic value type, so there is no public untyped iterator or raw-SQE entry
point. Operation-specific tickets provide typed iterator objects, items, and
preparation storage.

The private engine restores the generation-bearing operation table with its
first live consumer. `user_data` is `{generation, slot, tag}`, packed as tag 4,
slot 16, generation 44; tag zero is invalid so a mixed-CQE gap filler cannot
name a live record. A record holds its fixed owner, optional current waiter,
state, bounded `{res, cqe_flags}` delivery queue, and operation-specific
preparation/cleanup policy. Its slot returns to the free list only after the
terminal protocol and every control CQE are drained.

Iterator ownership is fixed to the opening fiber in this ticket. The object is
move-only by C contract, may have at most one active `next`, and is not copied
or consumed concurrently. Transfer is a later API decision, not an implicit
side effect of `next`.

`next` consumes a buffered delivery or parks on that record. A positive
terminal delivery returns ITEM and marks the iterator ended locally; the next
call returns END without consulting a recycled slot. A terminal zero that has
no operation-specific payload returns END. A terminal negative result returns
ERROR, translates to the current fiber's `errno`, and marks the iterator ended.

Destroying an unfinished iterator is synchronous cancel-and-drain. The cancel
request's CQE is not proof that the target ended
(`out/src/io_uring/cancel.c:210-244`); the original terminal event is observed
separately. Cancel CQEs may be ignored for target delivery only after their own
in-flight lifetime is accounted, so the scheduler cannot return with one still
inbound.

The scheduler loop distinguishes ready fibers, staged SQEs, live operation
records, and control requests. Submission is driven by `ring.batch_count`, not
by the number of parked fibers. Waking is edge-triggered through the waiter
relation: a completion wakes only a fiber blocked in `next` on that record; a
ready owner or an owner waiting elsewhere only accumulates delivery.

`__libuc_fiber_await` remains the fused one-item path. It uses the same terminal
lifetime rule where useful, but it is not implemented as open plus next plus
destroy; the await-NOP baseline is a standing regression guard.

The forcing source is `IORING_POLL_ADD_MULTI` over a pipe. It deliberately
produces no more deliveries than the test queue can hold. This proves routing
and iterator semantics, not a production overflow policy for unmetered
multishot poll.

## Acceptance

- Two `F_MORE` poll CQEs arrive before the owner runs; they are returned in
  order with one ready-queue insertion.
- A delivery arriving while the owner is ready buffers without double enqueue;
  an owner waiting on another record is not woken.
- A terminal item is returned once, retires its record once, and is followed by
  local END. A terminal error returns ERROR and is likewise followed by END.
- Destroy cancels and drains under both cancel-first and target-first CQE
  orderings. Repeated iterators recycle slots without table growth, and a stale
  generation cannot address the next tenant.
- No scheduler run returns with a live record or control CQE.
- The await-NOP benchmark remains within noise of its recorded baseline
  (~250 ns/op single, ~83 ns/op across 64 fibers).
- Both architectures build and test cleanly; the AArch64 forcing probe passes
  in the unconfined container and VM.
