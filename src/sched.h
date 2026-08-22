/*
 * The scheduler: cooperative tasks suspended on ring completions.
 *
 * The dispatch mechanism is the whole design: a ring op stamps
 * user_data = the task pointer, so the task IS the completion key — the
 * reap loop casts it back, delivers cqe->res into task->result, and marks
 * the task ready. No completion table, no lookup. Non-task completions,
 * when they appear, will claim tag bits in user_data; noted, not built.
 */
#ifndef RT_SCHED_H
#define RT_SCHED_H

#include "ring.h"
#include "task.h"

/* Create the scheduler's ring. Returns 0 or -errno straight from
 * rt_ring_setup, same conventions as ring.h. */
[[nodiscard]] int rt_sched_init(unsigned entries);

/* Ring ops, callable only from a task: stage an SQE, suspend until the reap
 * loop delivers the completion, return its res. A full SQ returns -EAGAIN
 * with no suspension — rt_ring_sqe's backpressure, surfaced.
 *
 * rt_write's off field is -1, the write(2) semantic: the kernel reads off
 * unconditionally (rw.c:272), and -1 selects the file's own position,
 * degrading to 0 for stream-mode files like the console (rw.c:483-493) —
 * where a literal 0 would mean "write at offset zero" on seekable files.
 *
 * raw_write is forbidden in task bodies; failure paths under the purity
 * registry's charter are the only exception.
 */
[[nodiscard]] int rt_nop(void);
[[nodiscard]] int rt_write(int fd, const void *buf, unsigned len);

/* Milestone 2's socket operations return the CQE result directly: a new fd or
 * zero on success, or -errno on failure. */
[[nodiscard]] int rt_socket(int domain, int type, int protocol);
[[nodiscard]] int rt_bind(int fd, const void *addr, unsigned addr_len);
[[nodiscard]] int rt_listen(int fd, int backlog);
[[nodiscard]] int rt_accept(int fd);
[[nodiscard]] int rt_recv(int fd, void *buf, unsigned len);
[[nodiscard]] int rt_close(int fd);

/* The loop, over caller-owned tasks: run every RT_READY task to its next
 * suspension point; publish the turn's staged SQEs and wait for one
 * completion; reap CQEs back into their tasks. Returns when every task is
 * RT_DEAD.
 *
 * Submission is batched once per turn — not an optimization: SQ_REWIND and
 * the in-kernel BPF loop both assume the loop owns submission timing, and
 * batching is what cached_sq_tail exists for. */
void rt_sched_run(struct rt_task **tasks, int ntasks);

#endif /* RT_SCHED_H */
