---
id: UC-017
title: Automatic iterator teardown and zombies
status: todo
depends: [UC-020]
---

## Goal

Turn an iterator owner that exits without explicit destruction into a zombie:
cancel its operations, drain every terminal event, dispose undelivered items,
and reclaim the fiber only afterward.

## Spec

UC-020 provides normal synchronous iterator destruction and the
generation-bearing operation identity. This ticket applies the same protocol
when the owner continuation no longer exists.

The load-bearing kernel fact is unchanged: a cancel CQE reports the cancel
request's result, not the target request's terminal event
(`out/src/io_uring/cancel.c:210-244`). A cancelled poll request is separately
kicked through its own completion path (`out/src/io_uring/poll.c:382-386`), and
queued io-wq work is separately failed with `-ECANCELED`
(`out/src/io_uring/io_uring.c:1489-1493`). Cancel-first and target-first are
both valid; neither permits early target-record reuse.

On fiber EXIT, detect every operation it still owns. Mark the fiber ZOMBIE,
remove it from runnable state, stage cancellation for each active operation,
and keep its stack and thread-local block mapped. Drain original terminal
events, required notifications, and cancel-request CQEs. Only when its owned
operation count and all cleanup work reach zero may the scheduler return its
storage to the owning pool.

The owner relation is the source of truth. Choose between an owner list and a
table scan using the expected many-iterators-per-fiber shape; record the choice
and complexity. A record may reuse its free-list link while FREE, but an ACTIVE
owner list must not make ordinary retirement quadratic.

Normal iterator code transfers ownership of yielded items to the caller.
Zombie draining instead invokes the record's typed cleanup policy for every
undelivered item. This hook is established here before accepted descriptors and
borrowed receive buffers land: later adapters close descriptors through the
ring and return buffer loans through their pool rather than teaching the
generic reap path their types.

The scheduler loop remains alive while ready fibers, live operation records,
cancel requests, zombies, or cleanup SQEs exist. A stale generation from a late
CQE must fail validation and can never address a recycled slot or fiber.

## Acceptance

- A poll-iterator owner returns without destroy. The scheduler makes it a
  zombie, cancels and drains the operation, and only then destroys the fiber.
- Cancel-first and target-first CQE orderings retire the target and cancel
  request exactly once.
- An undelivered synthetic typed item runs its cleanup hook before record or
  fiber reclamation.
- A stale CQE cannot address the slot's next tenant.
- The loop does not return while a zombie, operation, control request, or
  cleanup SQE remains.
- Both architectures build cleanly; AArch64 passes in the container and VM.
