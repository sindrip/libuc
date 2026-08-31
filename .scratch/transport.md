# Cross-scheduler transport

Status: **deferred proposal, reviewed 2026-08-30.** No second scheduler or
mailbox exists. Kernel claims were checked against the pinned 7.2 tree; mailbox
layout, backpressure, shutdown, and public semantics remain undesigned.

## Ownership determines the baseline

A scheduler does not free ordinary memory allocated by another scheduler.
Therefore zero-copy ownership transfer is not the starting point.

Two transfers respect the current invariant:

- **copy:** the receiver stores a copy in receiver-owned memory;
- **loan:** the receiver reads sender-owned immutable memory, acknowledges
  completion, and the sender eventually reclaims it.

Copy is the default because it keeps allocator ownership local. It still needs
a publication, capacity, acknowledgement/backpressure, and shutdown protocol;
“copy” solves memory ownership, not the whole transport.

## Proposed shape

Start with one bounded SPSC mailbox per sender-scheduler/receiver-scheduler
pair. The receiver owns the region's allocation and eventual release. The
sender is the sole producer and the receiver the sole consumer.

SPSC removes compare-and-swap contention, not synchronization. Producer and
consumer cursors cross Linux tasks and require the same release/acquire
publication discipline as any shared ring. An MPSC mailbox would additionally
need atomic reservation among senders; a pairwise SPSC matrix trades memory for
simpler ownership and ordering.

The message descriptor contains an offset and length into the mailbox plus a
kind/generation needed by the receiver. Payload capacity and alignment are
fixed by the mailbox, not by the doorbell.

## MSG_RING as doorbell

`IORING_OP_MSG_RING` delivers a CQE to the target scheduler's ring. The data
path carries `user_data` from `sqe->off` plus the 32-bit `len` result
(`out/src/io_uring/msg_ring.c:281-297`); flags can be passed separately with
`IORING_MSG_RING_FLAGS_PASS`. This is enough for a mailbox descriptor, not a
general payload.

A `DEFER_TASKRUN` target uses the remote task-work path
(`out/src/io_uring/msg_ring.c:68-93` and
`out/src/io_uring/tw.c:186-241`). A target waiting with one required completion
can therefore wake on one doorbell.

Caveats:

- remote posting allocates with `GFP_KERNEL` (`msg_ring.c:124`), so the sender
  can observe `-ENOMEM`;
- a target still created with `R_DISABLED` rejects the message with `-EBADFD`
  (`msg_ring.c:146`);
- the pure data path avoids the non-remote trylock/io-wq punt, but fd passing is
  a different operation and is not part of this proposal;
- suppressing the sender-side success CQE does not remove the need to handle
  submission failure or mailbox backpressure.

The kernel never receives a pointer to the mailbox slot through MSG_RING. Slot
lifetime is therefore governed by userspace publication and consumption, not by
waiting for the doorbell CQE to release a kernel-held address. Memory referenced
by a separate read/write/send operation still follows the terminal-CQE rule in
`scheduler.md`.

## Backpressure and failure

A bounded mailbox needs both directions of notification:

1. sender publishes a slot and rings the receiver when a transition requires a
   wake;
2. receiver consumes slots and notifies a blocked sender when capacity becomes
   available;
3. either side's scheduler shutdown closes the pair and resolves every blocked
   continuation.

Doorbells should be edge-triggered around empty/nonempty and full/nonfull
transitions, not one CQE per message. Lost wakeup avoidance and cursor wrap need
a small model or exhaustive test before implementation.

A doorbell completion must use the same tagged operation/key namespace designed
in UC-016, while remaining distinguishable from operation-table offsets. Raw
pointers and unversioned scheduler ids do not cross the ring.

## Large payloads

The first implementation copies every payload that fits. If measurements later
show copy cost, a loan holds sender memory until an acknowledgement. Immutable
shared blobs with home-scheduler reference accounting are a separate design;
they introduce explicitly shared state and must not be smuggled in as a “large
message optimization.”

True ownership transfer requires a recorded change to invariant 3. Plausible
mechanisms are dedicated transferable arenas with deferred remote return, or
whole-region handoff. Neither is needed to establish transport semantics.

## Implementation gate

Transport waits until all of these exist:

- a second scheduler and a lifecycle for its handle;
- UC-016's generation-bearing completion-key namespace;
- a bounded mailbox layout with release/acquire cursor rules;
- explicit sender and receiver shutdown behavior;
- acceptance tests for empty/nonempty, full/nonfull, receiver death, sender
  death, `-ENOMEM`, and stale doorbells.

Until then MSG_RING remains evidence that a ring-native doorbell exists, not a
finished cross-scheduler API.
