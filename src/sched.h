/*
 * The scheduler: RT-004's coroutines joined to RT-005's ring. A task
 * suspends on a ring operation, the reap loop matches its completion back
 * by user_data, and the loop resumes it — the first moment this is a
 * runtime rather than a pile of parts.
 *
 * Design doctrine (RT-006 ticket, amendments section): the reap loop here
 * is the userspace twin of the future in-kernel loop_step
 * (.scratch/bpf-loop.md) — same user_data encoding, same task-header
 * fields, same F_MORE rule — so the eventual lowering is a port, not a
 * redesign.
 */
#ifndef RT_SCHED_H
#define RT_SCHED_H

#include "ring.h"
#include "task.h"

/* TODO(1): The user_data encoding — slab byte offset | tag.
 *
 * Not a raw task pointer: an offset into a bounded slab is bounds-checkable
 * at reap time, stays decodable after task death (the teardown law: a slot
 * recycles only when inflight reaches zero), and is what a BPF verifier can
 * trust. rt_task's alignment leaves the low four bits free for the tag.
 * Milestone 1 defines TAG_OP only; TAG_MSG / TAG_LTIMEOUT / TAG_CANCEL are
 * reserved names (bpf-loop.md), not built.
 */

/* TODO(2): struct rt_sched — one scheduler, honestly scoped.
 *
 * The ring, a fixed task slab, and the running-task cursor. One static
 * instance in sched.c: the boot contract says init creates exactly one
 * scheduler, and milestone 3 multiplies instances as library code — it
 * must never need to restructure this. The slab is the registry crash.c's
 * e) seam has been waiting for: task identity is a slab index, and the
 * stack range lives in the rt_task.
 *
 * Milestone 1 needs no run queue: "run every RT_READY task" is a slab
 * scan, and the scan order is the declaration order determinism select
 * will later inherit.
 */

/* TODO(3): rt_sched_init — create the scheduler's ring. Returns 0 or
 * -errno straight from rt_ring_setup, same conventions as ring.h. */
int rt_sched_init(unsigned entries);

/* TODO(4): rt_spawn — take a free slab slot, rt_task_create into it.
 * Returns the slot index, or -1 with the slab full (a real contract, like
 * rt_ring_sqe's nullptr: milestone 3's backpressure point). */
int rt_spawn(void (*fn)(void *), void *arg);

/* TODO(5): The suspend protocol — rt_nop, rt_write.
 *
 * The ticket's sketch, amended for the encoding: take an SQE, prep the op,
 * user_data = the calling task's slab offset | TAG_OP, bump the task's
 * inflight, mark it RT_BLOCKED, switch to the scheduler. When the reap
 * loop resumes the task, the CQE's res is waiting in its header. rt_write
 * is the same shape over IORING_OP_WRITE — after milestone 1 wires it,
 * raw_write is forbidden outside the purity registry's uses.
 */
int rt_nop(void);
int rt_write(int fd, const void *buf, unsigned len);

/* TODO(6): rt_sched_run — the loop, the ticket's pseudocode made real:
 *
 *   run every RT_READY task to its next suspension point
 *   if nothing alive: return (rt_main falls into its idle loop)
 *   submit the staged SQEs and wait for one completion
 *   for each CQE: decode user_data (bounds-check the offset), skip the
 *     inflight decrement while CQE_F_MORE is set, write res/cqe_flags into
 *     the header, RT_BLOCKED -> RT_READY
 *
 * Submission is batched once per turn — not an optimization: it is what
 * keeps SQ_REWIND and the in-kernel loop reachable (both assume the loop
 * owns submission timing), and it is what cached_sq_tail was designed for.
 */
void rt_sched_run(void);

/* TODO(7): rt_sched_current — the running task, for crash.c's e) seam:
 * the dump names the task's slab index and stack range. Returns nullptr
 * between tasks (faults in the scheduler itself have no task to name). */
const struct rt_task *rt_sched_current(void);

#endif /* RT_SCHED_H */
