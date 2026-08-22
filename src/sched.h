/*
 * The scheduler: RT-004's coroutines joined to RT-005's ring. A task
 * suspends on a ring operation, the reap loop matches its completion back
 * by user_data, and the loop resumes it — the first moment this is a
 * runtime rather than a pile of parts.
 *
 * The design is the RT-006 spec's, deliberately minimal: user_data carries
 * the task pointer (the task IS the completion key — no table, no lookup),
 * tasks are caller-owned structs as in RT-004, and the one new channel is
 * task->result. The ticket's amendments section records the deferred
 * futures (offset encoding, header split, inflight) and why they wait.
 */
#ifndef RT_SCHED_H
#define RT_SCHED_H

#include "ring.h"
#include "task.h"

/* TODO(2): rt_sched_init — create the scheduler's ring. Returns 0 or
 * -errno straight from rt_ring_setup, same conventions as ring.h. */
int rt_sched_init(unsigned entries);

/* TODO(3): The suspend protocol — rt_nop, rt_write, the spec's sketch:
 *
 *   take an SQE (rt_ring_sqe), prep the op,
 *   user_data = (unsigned long)self — the task IS the completion key,
 *   self->state = RT_BLOCKED,
 *   switch to the scheduler; when it resumes us, return self->result.
 *
 * `self` comes from rt_current (task.c, TODO(1)). rt_write is the same
 * shape over IORING_OP_WRITE, four SQE fields: fd, addr (the buffer, cast
 * per house idiom), len, and off = -1 — the write(2) semantic. The kernel
 * reads off unconditionally (rw.c:272); -1 means the file's own position,
 * degrading to 0 for stream-mode files like the console (rw.c:483-493),
 * while a literal 0 would mean "write at offset zero" on seekable files.
 * After milestone 1 wires rt_write, raw_write is forbidden outside the
 * purity registry's uses.
 */
int rt_nop(void);
int rt_write(int fd, const void *buf, unsigned len);

/* TODO(4): rt_sched_run — the spec's loop over caller-owned tasks:
 *
 *   run every RT_READY task to its next suspension point
 *   if nothing alive: return (rt_main falls into its idle loop)
 *   submit staged SQEs, wait for one completion (rt_ring_submit_and_wait)
 *   for each CQE: t = (struct rt_task *)cqe->user_data;
 *                 t->result = cqe->res; t->state = RT_BLOCKED -> RT_READY
 *
 * Submission is batched once per turn — not an optimization: it is what
 * keeps SQ_REWIND and the in-kernel loop reachable (both assume the loop
 * owns submission timing), and it is what cached_sq_tail was designed for.
 */
void rt_sched_run(struct rt_task **tasks, int ntasks);

#endif /* RT_SCHED_H */
