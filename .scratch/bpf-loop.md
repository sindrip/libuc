# The in-kernel BPF loop — verified interface and the reap-loop lowering

Status: conversation-derived, 2026-08-18. Interface claims **verified against
`out/src/`** (cites inline); the lowering design itself is a proposal.
Promotes bpf.md direction 8, which now points here. Config is already
resolved on: `CONFIG_IO_URING_BPF=y`, `CONFIG_IO_URING_BPF_OPS=y`.

## The interface as 7.2 ships it

- One `struct_ops` (`io_uring_bpf_ops`) with a single callback, `loop_step`,
  bound to one ring by fd at registration (`bpf-ops.c:155-158, 162-180`).
- Installation **requires `DEFER_TASKRUN`** and rejects `SQPOLL`/`IOPOLL`
  (`bpf-ops.c:164-167`) — our ring config is the required one, again.
- Once installed, **every `io_uring_enter` on that ring runs the in-kernel
  loop** instead of the normal submit/wait path (`io_uring.c:2617-2620`).
  No flag; installing the ops changes what "enter" means for the ring.
- The kernel loop (`loop.c:42-78`): call `loop_step` with `uring_lock` held →
  on `IOU_LOOP_CONTINUE`, sleep until the CQ tail reaches the index the
  program wrote into `lp->cq_wait_idx` (`loop.c:6-10, 58-60`; the verifier
  explicitly allows writing that field, `bpf-ops.c:95-98`), run task work
  (which is what posts CQEs under `DEFER_TASKRUN`), step again.
  `IOU_LOOP_STOP` returns to userspace.
- Two kfuncs (`bpf-ops.c:17-50`):
  - `bpf_io_uring_submit_sqes(ctx, nr)` — submit from the SQ ring, in kernel,
    sleepable.
  - `bpf_io_uring_get_region(ctx, region_id, size)` — raw rdwr pointers to
    the CQ ring, the SQ ring, and **`param_region`, a userspace-registered
    shared memory region** (`register.c:712-746`). This is the shared-state
    channel.

## The lowering

Task *headers* — `{state, inflight, res, cqe_flags}` — live in a slab inside
`param_region`, alongside a ready-queue and a reclaim list. `loop_step` is the
scheduler's reap loop:

1. Walk new CQEs in the CQ region (bounded batch — verifier requires it).
2. Decode `user_data` as tag + offset; decrement `inflight` in the header
   slab (skip the decrement while `IORING_CQE_F_MORE` is set — multishot).
3. Zombie hitting zero → push its offset onto the reclaim list.
4. `TAG_OP` for a blocked task → write `res`/`cqe_flags`, mark READY, push
   onto the ready-queue. Bookkeeping tags (`TAG_LTIMEOUT`, `TAG_CANCEL`) →
   nothing.
5. `-EAGAIN`-class results → rewrite the SQE into the SQ region and
   `bpf_io_uring_submit_sqes` — the fiber never learns it happened.
6. Return `STOP` iff the ready-queue is non-empty; else set `cq_wait_idx`
   and `CONTINUE`.

The wake policy becomes programmable: userspace is entered exactly once per
"a fiber actually became runnable". Zombie drains, linked-timeout stragglers,
cancel confirmations, and transient-failure resubmits never wake the core.
Cross-core composes for free: a MSG_RING doorbell arrives as local task work
using the same `cq_wait_nr` wake the loop sleeps on (`tw.c:186-205`), so
remote messages wake the in-kernel loop exactly like local completions.

**What cannot lower**: running fibers (the context switch is userspace
register state) and memory reclamation itself (BPF flags a zombie
reclaimable; only userspace recycles arenas and stacks). The scheduler
bifurcates into a kernel data plane and a userspace control plane, split
exactly along the line the reap-loop encoding already drew.

## Encoding change — adopt regardless of BPF

`user_data` = **param-region offset | tag**, not raw pointer | tag. BPF
cannot trust raw user pointers, but an offset into a bounds-checked region is
verifier-friendly; userspace pays one add. It also makes every `user_data`
bounds-checkable in the plain-userspace scheduler — a debug win on its own.
`TAG_MSG` keeps its separate non-offset namespace (transport.md).

## What the program looks like

No in-tree reference exists (`tools/` in the pinned tree has no consumer of
`io_uring_bpf_ops`), so the shape below follows generic struct_ops practice —
SEC conventions are libbpf's and a freestanding loader defines its own
equivalents. Types come from a `vmlinux.h` generated from the pinned kernel's
BTF: `iou_ctx`, `iou_loop_params`, `io_uring_bpf_ops` are kernel-internal
(`bpf-ops.h`, `loop.h`), not uapi.

```c
/* rtloop.bpf.c — clang --target=bpf -O2 -g, against pinned-kernel vmlinux.h */
char LICENSE[] SEC("license") = "GPL";

extern int   bpf_io_uring_submit_sqes(struct iou_ctx *ctx, __u32 nr) __ksym;
extern __u8 *bpf_io_uring_get_region(struct iou_ctx *ctx, __u32 region_id,
                                     const size_t rdwr_buf_size) __ksym;

/* Layout mirrored in the C runtime; userspace fills the constants at init. */
struct task_hdr { __u32 state; __u32 inflight; __s32 res; __u32 cqe_flags; };
struct shm {
    __u32 cq_mask, cqes_off, ntasks;
    __u32 ready_tail;   __u32 ready[RQ_CAP];    /* BPF produces,       */
    __u32 reclaim_tail; __u32 reclaim[RQ_CAP];  /* userspace consumes  */
    struct task_hdr hdr[MAX_TASKS];
};

SEC("struct_ops.s/loop_step")     /* .s: sleepable, required for submit kfunc */
int BPF_PROG(rt_loop_step, struct iou_ctx *ctx, struct iou_loop_params *lp)
{
    __u8 *cqr = bpf_io_uring_get_region(ctx, IOU_REGION_CQ, CQ_REGION_SIZE);
    struct shm *shm = (void *)bpf_io_uring_get_region(ctx, IOU_REGION_MEM,
                                                      sizeof(struct shm));
    if (!cqr || !shm)
        return IOU_LOOP_STOP;                  /* fail open into userspace */

    struct io_rings *rings = (struct io_rings *)cqr;
    __u32 head = rings->cq.head;
    __u32 tail = __atomic_load_n(&rings->cq.tail, __ATOMIC_ACQUIRE);
    struct io_uring_cqe *cqes = (void *)(cqr + shm->cqes_off);
    bool runnable = false;

    for (int i = 0; i < BATCH && head != tail; i++, head++) {
        struct io_uring_cqe *cqe = &cqes[head & shm->cq_mask];
        __u32 tag = cqe->user_data & TAG_MASK;
        __u32 idx = (cqe->user_data & ~(__u64)TAG_MASK) / sizeof(struct task_hdr);

        if (tag == TAG_MSG) { /* doorbell: mailbox offset, no task header */
            runnable |= note_mailbox(shm, cqe);
            continue;
        }
        if (idx >= shm->ntasks)                /* safety + verifier bound */
            continue;
        struct task_hdr *t = &shm->hdr[idx];

        if (!(cqe->flags & IORING_CQE_F_MORE)) /* multishot: last CQE only */
            t->inflight--;

        if (t->state == RT_ZOMBIE) {
            if (t->inflight == 0)
                push(shm->reclaim, &shm->reclaim_tail, idx);
        } else if (t->state == RT_BLOCKED && tag == TAG_OP) {
            t->res = cqe->res; t->cqe_flags = cqe->flags;
            t->state = RT_READY;
            push(shm->ready, &shm->ready_tail, idx);
            runnable = true;
        }
        /* TAG_LTIMEOUT / TAG_CANCEL: the decrement was the whole job */
    }
    __atomic_store_n(&rings->cq.head, head, __ATOMIC_RELEASE);

    if (runnable || reclaim_pending(shm))
        return IOU_LOOP_STOP;                  /* fibers to run: wake up */
    lp->cq_wait_idx = tail + 1;                /* a hint; early wakes fine
                                                  (loop.h:8-11) */
    return IOU_LOOP_CONTINUE;
}

SEC(".struct_ops.link")
struct io_uring_bpf_ops rt_loop = {
    .loop_step = (void *)rt_loop_step,
    /* .ring_fd set by the loader before registration (bpf-ops.c:146-160) */
};
```

Open validation items for the sketch: whether `loop_step` may advance
`cq.head` itself (it holds `uring_lock`; the posting side reads head for
overflow decisions — check `io_uring.c` overflow paths); `shm->cqes_off` is
userspace-provided and must be bounds-checked against the region size or the
verifier (rightly) refuses; ring-resize (`rings_rcu`) interaction with a held
region pointer. The `-EAGAIN` resubmit branch is elided above: write the SQE
into the SQ region, bump its tail, `bpf_io_uring_submit_sqes(ctx, 1)`.

## Costs and gates

- `bpf(2)` on invariant 1's direct-syscall list (recorded in bpf.md; pending
  discussion).
- Verifier discipline: bounded CQE walk, provably in-bounds region access —
  the `bpf fn` dialect's restriction set (language.md) showing up as a real
  requirement years early. Until the language exists this is hand-written
  BPF C compiled with Clang's BPF target.
- **The real ticket is the loader.** No libc means no libbpf: loading a
  struct_ops program by hand means BTF parsing, map/link creation, and
  attachment via raw `bpf(2)` — likely comparable in effort to the ring
  bootstrap itself. Budget for it as its own work item, sequenced after the
  plain-userspace scheduler works (the BPF loop is an optimization of a
  working loop, not a substitute for writing one).
