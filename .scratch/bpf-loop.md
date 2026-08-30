# The in-kernel BPF loop

Status: **verified kernel interface plus deferred proposal, 2026-08-30.** The
plain userspace reactor is implemented. This optimization waits on UC-016's
operation records and on a freestanding BPF loader. Kernel claims below were
checked against the pinned 7.2 tree.

## Interface shipped by 7.2

One `io_uring_bpf_ops` struct_ops object supplies a `loop_step` callback and is
bound to a ring fd during registration
(`out/src/io_uring/bpf-ops.c:155-180`). Installation requires
`IORING_SETUP_DEFER_TASKRUN` and rejects SQPOLL/IOPOLL
(`out/src/io_uring/bpf-ops.c:162-167`).

Once installed, every `io_uring_enter` on that ring calls `io_run_loop` before
the ordinary submission/wait path and returns from there
(`out/src/io_uring/io_uring.c:2618-2621`). The loop therefore replaces normal
userspace submission for that ring; it is not an optional reap hook.

The kernel loop (`out/src/io_uring/loop.c:42-78`) calls `loop_step` with the
uring lock held. The callback returns:

- `IOU_LOOP_CONTINUE`: sleep until the CQ tail reaches the callback's
  `cq_wait_idx`, run deferred task work, then call the step again;
- `IOU_LOOP_STOP`: return from `io_uring_enter` to userspace.

The verifier permits the callback to write `cq_wait_idx`
(`out/src/io_uring/bpf-ops.c:95-98`).

Two kfuncs form the data plane (`out/src/io_uring/bpf-ops.c:17-50`):

- `bpf_io_uring_submit_sqes(ctx, nr)` submits SQEs from the ring;
- `bpf_io_uring_get_region(ctx, region_id, size)` returns bounded read/write
  access to the CQ region, SQ region, or a userspace-registered parameter
  region (`out/src/io_uring/register.c:712-746`).

The current resolved kernel configuration already contains
`CONFIG_IO_URING_BPF=y` and `CONFIG_IO_URING_BPF_OPS=y`.

## Relationship to the userspace reactor

The BPF callback may lower bounded bookkeeping, not fiber execution:

1. inspect a bounded number of new CQEs;
2. validate and decode the completion key into UC-016's operation slab;
3. append opcode-specific delivery and update terminal bookkeeping;
4. put a waiting owner on the ready queue only on the operation's defined wake
   edge;
5. put fully drained zombies on a userspace reclaim list;
6. submit the SQ batch through `bpf_io_uring_submit_sqes`;
7. stop when userspace has ready fibers or reclamation work, otherwise set the
   next CQ index and continue sleeping in-kernel.

Context switching, running fibers, allocating slab chunks, returning buffers,
and reclaiming stacks or arenas remain userspace work.

This is an optimization of a correct userspace state machine. The userspace
and BPF paths must consume the same operation-record layout and pass the same
forcing tests; there is no second BPF-specific completion identity space.

## Completion-key requirement

Raw fiber pointers are unsuitable for BPF validation and already disappear in
UC-016. The shared completion key is:

```text
generation | operation-slot offset | tag
```

The offset addresses a record in a bounded BPF-visible window of the
per-scheduler operation slab. The generation rejects stale CQEs after slot
reuse, and the tag distinguishes primary delivery, cancellation, linked
timeouts, notifications, and transport.

The parameter region need not contain every fiber ever allocated. It may expose
a bounded active window, multiple registered regions, or a reserved address
range. The BPF encoding must not silently impose a global maximum fiber count.

`IORING_CQE_F_SKIP` is ignored before key decoding only when mixed CQEs are
enabled; otherwise it is configuration drift. `F_MORE` and `F_NOTIF` are
operation-kind inputs. A terminal CQE can still carry a final result and must be
delivered before its record retires.

## What must not be lowered generically

Do not automatically resubmit every completion returning `-EAGAIN`. A final
`-EAGAIN` can be requested and caller-visible behavior, while many retryable
paths are already handled inside io_uring. Any resubmission policy must belong
to a specific operation kind and retain all SQE preparation state needed to
issue it again.

Do not decrement a single generic in-flight count merely because `F_MORE` is
clear. Single-shot, streams, linked timeouts, cancellation, and zero-copy send
notifications have different terminal protocols. UC-016 defines those state
machines before the BPF lowering copies them.

Do not reclaim memory in BPF. Reclaimability is a notification to userspace;
only the scheduler returns memory to its own pools.

## Validation still required

Before this becomes a ticket, verify in the pinned tree and with a minimal
program:

- whether `loop_step` may advance CQ head directly without breaking overflow
  accounting;
- the required acquire/release operations for CQ and parameter-region queues;
- how ring resize affects pointers returned by `get_region`;
- how the callback observes the exact number of userspace-prepared SQEs under
  `SQ_REWIND`;
- verifier bounds for slab lookup, ready delivery, and reclaim production;
- ordering between MSG_RING task work and local CQEs;
- failure behavior when the BPF program or a kfunc returns an error.

No in-tree userspace reference loader exists. The program needs kernel-internal
types derived from the pinned BTF, not copied into UAPI-like local declarations.

## Loader and purity cost

The runtime must add `bpf(2)` to the enumerated direct-syscall allowlist before
shipping this; no ring opcode loads BPF. That requires the invariant discussion,
not a purity-exception entry.

With no libbpf, the loader must parse enough BTF/ELF metadata, create maps and
links, load the struct_ops program, and attach it to the ring. C23 `#embed` can
carry the compiled BPF object in the runtime's `.rodata`, but it does not remove
the loader work.

The implementation order is therefore:

1. UC-016 operation identity and userspace multi-CQE tests;
2. UC-017 cancellation/zombie lifetime;
3. a minimal freestanding struct_ops loader;
4. the BPF callback running the same state-machine fixtures;
5. measurement showing fewer user/kernel transitions or better tail latency.

Without the final measurement, installing the loop adds a privileged loader and
a second execution environment without a demonstrated benefit.
