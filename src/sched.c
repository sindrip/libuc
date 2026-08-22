/*
 * Scheduler bodies. Contracts are in sched.h; the ring the tasks suspend on
 * lives here, file-static — one ring, one scheduler, one thread.
 */

#include "sched.h"

#include <stdatomic.h>
#include <stdint.h>

#include <asm/errno.h> /* EAGAIN */

#include "crash.h"
#include "syscall.h"

static struct rt_ring ring;

int rt_sched_init(unsigned entries) { return rt_ring_setup(&ring, entries); }

/* The suspend tail shared by every ring op: stamp the completion key, mark
 * blocked, hand the CPU to the scheduler. When the reap loop resumes this
 * task, the CQE's res is already in self->result — the return below runs
 * arbitrarily later, and the caller cannot tell it was ever gone. */
static int rt_suspend(struct rt_task *self, struct io_uring_sqe *sqe) {
  sqe->user_data = (unsigned long)(uintptr_t)self;
  self->state = RT_BLOCKED;
  rt_switch(&self->ctx, &rt_sched_ctx);
  return self->result;
}

int rt_nop(void) {
  struct rt_task *self = rt_current;
  struct io_uring_sqe *sqe = rt_ring_sqe(&ring);
  if (sqe == nullptr) {
    return -EAGAIN; /* honest backpressure, no suspension happened */
  }

  sqe->opcode = IORING_OP_NOP;
  return rt_suspend(self, sqe);
}

int rt_write(int fd, const void *buf, unsigned len) {
  struct rt_task *self = rt_current;
  struct io_uring_sqe *sqe = rt_ring_sqe(&ring);
  if (sqe == nullptr) {
    return -EAGAIN;
  }

  sqe->opcode = IORING_OP_WRITE;
  sqe->fd = fd;
  sqe->addr = (unsigned long)(uintptr_t)buf;
  sqe->len = len;
  /* -1, not 0: the write(2) semantic. The kernel reads off unconditionally
   * (rw.c:272); -1 selects the file's own position (rw.c:483-493). */
  sqe->off = (__u64)-1;
  return rt_suspend(self, sqe);
}

void rt_sched_run(struct rt_task **tasks, int ntasks) {
  for (;;) {
    /* Run every ready task to its next suspension point, and take stock
     * afterwards — a task's state after resume is where it suspended TO:
     * READY (yielded), BLOCKED (staged an op), or DEAD. */
    bool alive = false;
    bool blocked = false;
    for (int i = 0; i < ntasks; i++) {
      if (tasks[i]->state == RT_READY) {
        rt_sched_resume(tasks[i]);
      }
      alive |= tasks[i]->state != RT_DEAD;
      blocked |= tasks[i]->state == RT_BLOCKED;
    }

    if (!alive) {
      return; /* last task died; rt_main falls into its idle loop */
    }
    if (!blocked) {
      continue; /* yield-only round: nothing for the kernel, scan again */
    }

    /* Publish and wait. The staged count is the private cursor's lead over
     * the published tail: every op staged this turn goes to the kernel in
     * one enter. */
    auto staged = ring.cached_sq_tail -
                  atomic_load_explicit(ring.sq_tail, memory_order_relaxed);
    auto ret = rt_ring_submit_and_wait(&ring, staged, 1);
    if (sys_failed(ret)) {
      rt_panic("sched: enter failed", __builtin_return_address(0));
    }

    /* Reap: the CQE's user_data is the task — cast it back, deliver the
     * result, mark ready. The next scan resumes it. */
    struct io_uring_cqe cqe;
    while (rt_ring_reap(&ring, &cqe)) {
      struct rt_task *t = (struct rt_task *)(uintptr_t)cqe.user_data;
      t->result = cqe.res;
      t->state = RT_READY;
    }
  }
}
