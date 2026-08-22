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

int rt_socket(int domain, int type, int protocol) {
  struct rt_task *self = rt_current;
  struct io_uring_sqe *sqe = rt_ring_sqe(&ring);

  /* TODO(libuc): every ring wrapper needs to hide SQ-capacity backpressure
   * instead of exposing it as an operation result. A full SQ means other
   * tasks have staged work; yield so the scheduler can publish that work and
   * the kernel can advance sq_head, then retry:
   *
   *   struct io_uring_sqe *sqe;
   *   while ((sqe = rt_ring_sqe(&ring)) == nullptr) {
   *     rt_yield();
   *   }
   */
  if (sqe == nullptr) {
    return -EAGAIN;
  }

  sqe->opcode = IORING_OP_SOCKET;

  sqe->fd = domain;
  sqe->off = (unsigned)type;
  sqe->len = (unsigned)protocol;

  return rt_suspend(self, sqe);
}

int rt_bind(int fd, const void *addr, unsigned addr_len) {
  struct rt_task *self = rt_current;
  struct io_uring_sqe *sqe = rt_ring_sqe(&ring);
  /* TODO(libuc): use the cooperative SQ-space retry sketched in rt_socket. */
  if (sqe == nullptr) {
    return -EAGAIN;
  }

  sqe->opcode = IORING_OP_BIND;
  sqe->fd = fd;
  sqe->addr = (unsigned long)(uintptr_t)addr;
  sqe->addr2 = addr_len;

  return rt_suspend(self, sqe);
}

int rt_listen(int fd, int backlog) {
  struct rt_task *self = rt_current;
  struct io_uring_sqe *sqe = rt_ring_sqe(&ring);
  /* TODO(libuc): use the cooperative SQ-space retry sketched in rt_socket. */
  if (sqe == nullptr) {
    return -EAGAIN;
  }

  sqe->opcode = IORING_OP_LISTEN;
  sqe->fd = fd;
  sqe->len = (unsigned)backlog;

  return rt_suspend(self, sqe);
}

int rt_accept(int fd) {
  struct rt_task *self = rt_current;
  struct io_uring_sqe *sqe = rt_ring_sqe(&ring);
  /* TODO(libuc): use the cooperative SQ-space retry sketched in rt_socket. */
  if (sqe == nullptr) {
    return -EAGAIN;
  }

  sqe->opcode = IORING_OP_ACCEPT;
  sqe->fd = fd;
  /* The cleared addr/addr2 fields discard the peer address. Cleared ioprio
   * makes this one-shot: multishot CQEs do not fit today's task wakeup model. */

  return rt_suspend(self, sqe);
}

int rt_close(int fd) {
  struct rt_task *self = rt_current;
  struct io_uring_sqe *sqe = rt_ring_sqe(&ring);
  /* TODO(libuc): use the cooperative SQ-space retry sketched in rt_socket. */
  if (sqe == nullptr) {
    return -EAGAIN;
  }

  sqe->opcode = IORING_OP_CLOSE;

  sqe->fd = fd;

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
     * one enter.
     *
     * Success means the whole batch, not merely a nonnegative return. The
     * kernel may stop after a request-allocation failure or a bad SQE and
     * return a positive short count (io_uring.c:2046-2068); io_uring_enter
     * then skips the wait (io_uring.c:2646-2650). The shared tail already
     * exposes the entire batch, so carrying on would make the next staged
     * calculation zero while the unconsumed suffix remains behind sq_head,
     * stranding those tasks in RT_BLOCKED forever.
     *
     * Retrying needs submission accounting based on cached_sq_tail - sq_head.
     * This milestone does not implement that state machine, so fail loudly. */
    auto staged = ring.cached_sq_tail -
                  atomic_load_explicit(ring.sq_tail, memory_order_relaxed);
    auto ret = rt_ring_submit_and_wait(&ring, staged, 1);
    if (sys_failed(ret)) {
      rt_panic("sched: enter failed", __builtin_return_address(0));
    }
    if ((unsigned)ret != staged) {
      rt_panic("sched: short submit", __builtin_return_address(0));
    }

    /* Reap: the CQE's user_data is the task — cast it back, deliver the
     * result, mark ready. The next scan resumes it.
     *
     * The state check codifies this milestone's one-shot contract: a task
     * stages exactly one SQE and becomes RT_BLOCKED before the scheduler can
     * observe its CQE. A duplicate or stale CQE must not overwrite result or
     * make an already-runnable task ready again. This is a tripwire, not the
     * future multishot/cancellation model: RT_BLOCKED identifies a task state,
     * not which operation it is awaiting. */
    struct io_uring_cqe cqe;
    while (rt_ring_reap(&ring, &cqe)) {
      struct rt_task *t = (struct rt_task *)(uintptr_t)cqe.user_data;
      if (t->state != RT_BLOCKED) {
        rt_panic("sched: cqe for nonblocked task", __builtin_return_address(0));
      }
      t->result = cqe.res;
      t->state = RT_READY;
    }
  }
}
