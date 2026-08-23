# Cross-core transport

Status: **conversation-derived proposal, 2026-08-18. Not argued through in
plan.md.** Addresses plan.md's open question "cross-scheduler message transport and
buffer ownership". Kernel claims verified against `out/src/` 2026-08-18; cites
inline.

## The invariant decides the design

Invariant 3 forbids allocating on one scheduler and freeing on another. Ownership *transfer* of a
buffer between cores means the receiver eventually frees memory the sender
allocated — a cross-scheduler free by definition. Therefore:

**Copy-on-send is the simplest transport the invariants permit.** Not the only
one: a *loan* — receiver reads sender-owned memory in place, acks, sender frees
— also has no cross-scheduler alloc or free, at the cost of an ack protocol and
sender memory held until it completes. Copy is the right default because it
needs no protocol and BEAM has run on exactly this model for three decades; the
loan is the fallback if profiles ever show copy cost, and it is *cheaper* to
reach than ownership transfer because it needs no exception to invariant 3.

## Shape

- Per receiver (or per sender→receiver pair), a **mailbox region owned by the
  receiving core**: allocated and freed only by its owner. Senders copy
  payloads in; the region is the "explicitly designated shared state" that
  invariant 3 already provides for. Shape matters: a single per-receiver MPSC
  mailbox needs *atomic* cursor claims by competing senders; SPSC per
  sender→receiver pair needs no atomics on the write side at the price of N²
  regions. Start SPSC — core counts are small and fixed.
- `MSG_RING` is the doorbell. Payload verified against
  `out/src/io_uring/msg_ring.c:281-297`: `user_data` (from `sqe->off`, 64
  bits) + `len` (32 bits, arrives as `cqe->res`) = 96 bits, plus 32 bits of
  `cqe_flags` via `IORING_MSG_RING_FLAGS_PASS` — enough for an offset/length
  into the mailbox, not for data. The receiving core learns of the message the
  same way it learns everything: a CQE in its own ring.
- **Delivery into a `DEFER_TASKRUN` target: VERIFIED, and the kernel requires
  it rather than merely tolerating it.** `msg_ring.c:68-71` routes targets
  with task-complete semantics to the remote path; `tw.c:236-241`
  (`io_req_task_work_add_remote`) `WARN`s unless the target is
  `DEFER_TASKRUN`. Wakeup: the post carries `IOU_F_TWQ_LAZY_WAKE`
  (`msg_ring.c:93`), and `tw.c:186-205` counts `cq_wait_nr` down — a receiver
  parked in `io_uring_enter` with `min_complete=1` wakes on a single
  doorbell. Caveats found while reading:
  - the remote post allocates (`GFP_KERNEL`, `msg_ring.c:124`) — `-ENOMEM` is
    a possible sender-side result; the doorbell is not allocation-free.
  - a target still `R_DISABLED` returns `-EBADFD` (`msg_ring.c:146`):
    enable rings before any cross-scheduler doorbell (interacts with the
    RESTRICTIONS sealing sequence in bpf.md).
  - the pure-data path to a `DEFER_TASKRUN` target never punts to io-wq — the
    trylock/`-EAGAIN` punt (`msg_ring.c:40-55`) is only for non-remote/IOPOLL
    targets and fd-passing. Use `IORING_MSG_DATA` only and io-wq stays
    out of it.
  - `IORING_MSG_RING_CQE_SKIP` suppresses the sender-side CQE — doorbells can
    be fire-and-forget. And `io_uring_sync_msg_ring` (`msg_ring.c:335`, via
    `IORING_REGISTER_SEND_MSG_RING`) can send data *without a source ring* —
    useful for pre-scheduler or crash-path signalling.
- Large payloads: BEAM's split applies. Copy small messages; large ones live in
  a designated shared region with a refcount, and only the *reference* is
  copied. The threshold is a measurement, not a decision — start with
  everything copied.

## Transfer as a later, registered exception

Zero-copy ownership transfer (the future language's `iso` move) is an
*optimization* over copy-on-send with identical observable semantics — so it
can land later without breaking any program. If profiles ever show copy cost
that matters, the carve-out is one of:

- **Message arenas**: transferable messages allocate from a dedicated arena
  whose blocks return to the owning core via a flushed free-list
  (mimalloc-style deferred remote free), or
- **Region handoff**: whole arenas change owner at once, amortizing the
  cross-scheduler accounting to one event per region.

Either is a deliberate exception to invariant 3 and goes through the same
"discuss before violating" gate as everything else — a registry entry, not a
quiet erosion.

## Buffer lifetime: the reap rule

Anything handed to the kernel — a mailbox slot, a read buffer, an SQE's
address — stays kernel-visible until its CQE is reaped. Cancellation does not
change this: `ASYNC_CANCEL` is a request, and the original operation still
completes (with its result or `-ECANCELED`) and may write into the buffer
until then. Therefore the universal rule:

**A task is not done until its in-flight CQEs are drained. Cancel → drain →
only then release memory.** This applies equally to deadline cancellation,
crash teardown, and scope joins — "the function unwound" is not "the task is
done".

The structural fix that makes the rule cheap: kernel-visible *receive* buffers
should be owned by per-scheduler pools, not by task-lifetime memory. plan.md's
pending milestone-2 decision on `IORING_REGISTER_PBUF_RING` is this same
question — provided buffer rings are exactly core-owned receive buffers — so
the two should be decided together.

## Sequencing

Milestone 3 (multi-core) needs only the doorbell. Milestone 4 (cross-scheduler
transport, plan.md) needs the mailbox. Transfer needs a profiler showing copies
on a flame graph, and not before.
