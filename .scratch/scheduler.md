# The scheduler — current contract and destination seams

Status: **current design record, 2026-08-30.** The private single-scheduler
reactor is implemented through UC-015. Public topology, scheduler spawning,
operation tables, iterator teardown, and cross-scheduler transport are future
work and are labelled as such below.

## Current private surface

```c
[[nodiscard]] bool
__libuc_scheduler_become(struct __libuc_scheduler *scheduler);

void __libuc_scheduler_enqueue(struct __libuc_scheduler *scheduler,
                               struct __libuc_fiber *fiber);

void __libuc_scheduler_run(struct __libuc_scheduler *scheduler);
```

The caller supplies stable scheduler storage. `become` initializes it and
creates a ring owned by the calling Linux task. It does not change affinity,
publish an id, allocate a registry entry, or enter the loop.

The scheduler currently owns:

- its ring;
- an intrusive FIFO of ready fibers;
- `ready` and `parked`, which count the two places a live fiber can stand under
  the single-CQE contract.

A fiber's record is carved from the top of its own stack mapping, so the
pointer exists exactly as long as the fiber does. UC-011 must still make
scheduler ownership explicit before a per-scheduler pool can replace the
current per-fiber mappings.

What a fiber asks its scheduler for lives on the asking frame, which the
suspension keeps alive: the context switch carries the address of a
`{kind, argument, result, fiber}` request, and the scheduler answers in
place. Nothing about a request is stored in the fiber, so none of it is
readable outside the window in which it means something. One-shot
`user_data` therefore carries the request pointer rather than a fiber
pointer, and the request records which fiber to resume. SPAWN is one of
these requests, not a separate path.

Scheduler zero lives in the active startup frame on the kernel-provided stack.
Startup calls `become`, creates and enqueues the root fiber, runs the loop,
destroys the root after the loop returns, and carries `main`'s status to
`exit_group`. Scheduler code runs with the bootstrap thread pointer restored
and must not touch compiler-generated `_Thread_local` state.

## One reactor iteration

At the start of an iteration, snapshot `ready`. Dispatch exactly that many
fibers; fibers that yield re-enter the tail for the next generation. This bound
prevents a persistent yielder from starving submitted work.

Each dispatch produces one request:

| request | scheduler action |
|---|---|
| `YIELD` | append the fiber to ready |
| `EXIT` | leave it unqueued; its caller may reclaim it after the loop returns |
| `AWAIT` | copy the fiber-frame SQE into the ring batch and increment `parked` |
| `SPAWN` | create the fiber, answer the spawner, and queue the spawner ahead of what it made |

There is no `NONE`: a kind rides the switch, so it cannot be stale or
unset. Zero is what a resume passes in, so seeing it come back is a
broken transfer and traps.

`SPAWN`'s ordering is not a preference. C11 7.26.5.1 makes `thrd_create`'s
completion synchronize with the start of the new thread, so the spawner
must be resumed far enough to store the handle before its child can read
it.

After the generation:

- when `parked == 0`, continue without entering the kernel;
- when fibers remain ready, submit with `min_complete = 0`;
- when the ready queue drained, submit and wait with `min_complete = 1`;
- reap every available CQE, write its result to the parked fiber, decrement
  `parked`, and enqueue the fiber once.

The current loop returns when `ready + parked == 0`. That is a probe-era
termination contract, not resident scheduler shutdown. UC-020 adds operation
lifetime and UC-017 adds zombies, so either makes termination depend on more
than the two fiber-location counters.

## The request protocol

A fiber does not mutate scheduler queues or the ring. It writes a request in
its own record and transfers control to its resumer. The scheduler is the sole
handler and the sole writer of scheduler-owned state.

This is usefully understood as a fixed effect handler:

| effect term | runtime representation |
|---|---|
| operation | the fiber request enumerator and payload |
| continuation | saved registers plus the live fiber stack |
| perform | publish the request and switch to the resumer |
| handler | scheduler dispatch or CQE reap |
| result | scheduler writes the fiber/operation result before resumption |

It is not a general algebraic-effects system. There is one handler, no handler
composition, no continuation cloning, and no migration between handlers. The
model is valuable because queue membership is ownership of the continuation,
not merely a scheduling hint.

Admission rule for future requests:

> A request belongs in this protocol when it transfers the continuation or
> needs authority over scheduler-owned state. Otherwise it is an ordinary
> private call.

No `default` belongs in a switch over the request enum. The build retains
`-Wcovered-switch-default` and suppresses `-Wswitch-default`, so adding an
enumerator fails every uncovered dispatch site at compile time.

## Buffer and SQE lifetime

`__libuc_fiber_await` accepts one SQE stored in the calling frame. The fiber
suspends before that frame can return, and the scheduler copies the SQE before
the fiber is resumed. Every address referenced by the SQE must remain valid and
exclusively available until the completion is observed.

The continuation protects only its own frame. It does not prevent another
fiber from accessing an aliased static or heap buffer. C cannot express that
loan, so the public libc boundary remains an auditable protocol rather than a
safe API.

Cancellation does not shorten the kernel-visible lifetime. A cancel request
has its own CQE; the original request separately reaches its terminal CQE. The
universal rule is:

```text
cancel -> observe the original terminal event -> reclaim referenced memory
```

This applies to stacks, arenas, buffers, operation records, linked timeouts,
and zero-copy notifications. It is the reason a dead fiber becomes a zombie
rather than being freed immediately once operations can outlive its call frame.

## Completion identity

Today `sqe->user_data` is the parked fiber pointer. That is valid only because:

- one fiber can await at most one operation;
- the operation produces exactly one CQE;
- the fiber cannot exit while parked;
- the fiber record remains stable through the wait.

UC-020 ends that contract. The completion key becomes
`{generation, operation slot, tag}` and indexes a per-scheduler operation table.
The record, not the fiber, lives until the operation's terminal event. It owns:

- its fiber owner and current waiter, if any;
- generation and staged/active/terminal state;
- typed preparation and cleanup policy, including terminal bookkeeping;
- a bounded result-delivery queue;
- any preparation data needed until the kernel releases referenced memory.

The generation detects a CQE aimed at a recycled slot; it does not permit early
recycling. A slot returns to the free list only after its terminal protocol is
complete. A fiber's stack and thread-local block return only after it owns zero
live operations.

`F_MORE` means another CQE follows this one. Clearing `F_MORE` makes the current
CQE terminal; it does not erase that CQE's payload. `F_NOTIF` participates in
the zero-copy send protocol. `F_SKIP` is ignored only on a mixed-CQE ring where
the kernel may insert a wrap filler; on the current ring it is a fatal format
mismatch.

The public repeated-value shape is a typed pull iterator: `next` returns item,
end, or error; destroying an unfinished iterator cancels and drains it. A
positive terminal delivery still returns its item, then makes the following
`next` return local end. C has no generic value type, so the scheduler provides
the shared record machinery while operation-specific adapters define their
typed items. `__libuc_fiber_await` remains the fused one-item path rather than
paying open/next/destroy dispatches.

## Future scheduler construction

There is deliberately no public scheduler header yet. A complete surface must
answer lifecycle and registry questions before exposing handles.

The constraints already settled are:

- an existing Linux task may become a scheduler without changing its affinity;
- a library-created scheduler is a new Linux task that calls `become` on itself;
- placement is policy, not scheduler identity;
- spawning normally narrows the child to a requested CPU, but correctness is
  task-based rather than CPU-based;
- a scheduler id is not needed until another scheduler can consume it;
- slot generations are earned only when scheduler destruction and reuse exist;
- a public failure convention waits for per-fiber `errno` rather than exposing
  raw negative kernel results.

`SINGLE_ISSUER` stores the submitting task, not a CPU
(`out/src/io_uring/io_uring.c:3065-3067`). Pinning is therefore a locality and
wakeup-placement policy. It does not authorize naming cores as owners: rings,
pools, fibers, and operation tables belong to schedulers.

## No migration

No fiber moves between schedulers. This is a design invariant even in states
where the kernel would not detect a move.

An in-flight operation already makes movement impossible without forwarding,
because its CQE returns to the issuing ring and the kernel may retain pointers
into the fiber's memory. A ready fiber without an operation is still immobile
because scheduler-owned allocation, stack pools, TLS storage, provided buffers,
wait queues, and future arenas all assume creation and reclamation on one
scheduler.

The code should avoid accidentally foreclosing a different design, but that is
not permission to implement it. Any handoff of a fiber record, stack, or
continuation to another scheduler requires the invariant discussion in
AGENTS.md before code changes.
