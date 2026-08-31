#ifndef LIBUC_SRC_FIBER_FIBER_H
#define LIBUC_SRC_FIBER_FIBER_H

#include <stddef.h>

#include "fiber_arch.h"
#include "thread_local/thread_local.h"

enum __libuc_fiber_request : unsigned long {
  __LIBUC_FIBER_REQUEST_NONE,
  __LIBUC_FIBER_REQUEST_YIELD,
  __LIBUC_FIBER_REQUEST_EXIT,
  __LIBUC_FIBER_REQUEST_AWAIT,
  __LIBUC_FIBER_REQUEST_SPAWN,
};

/* A SPAWN request's argument, living on the requesting fiber's suspended
 * frame; the scheduler writes back the fiber it made, or nullptr. */
struct io_uring_sqe;

struct __libuc_fiber_spawn_request {
  size_t stack_length;
  int (*entry)(void *);
  void *argument;
  struct __libuc_fiber *spawned;
};

struct __libuc_fiber {
  /* where the fiber stopped */
  struct fiber_context context;
  int (*entry)(void *);
  void *argument;
  unsigned char *stack;
  size_t stack_length;
  struct __libuc_thread_local_block thread_local_block;
  /* The pending resume's frame; dead between resumes. */
  struct fiber_context *return_to;
  /* Scheduler-owned FIFO link; a fiber is in at most one queue. */
  struct __libuc_fiber *ready_next;
  /* The suspended frame's request argument — an SQE for AWAIT, a spawn
   * request for SPAWN. The scheduler consumes it before any resume. */
  const void *request_argument;
  /* The request's answer: a completion's res, or the spawned fiber. The
   * scheduler stores it before resuming. */
  long request_result;
  enum __libuc_fiber_request request;
  /* The entry's return value, readable once the fiber has exited. */
  int status;
  uint32_t : 32;
};

/* Create a fiber whose record lives at the top of its own stack mapping,
 * so the stack grows down away from it. nullptr when the mapping fails or
 * the stack cannot hold the record; destroying the fiber invalidates the
 * pointer along with the mapping. */
[[nodiscard]] struct __libuc_fiber *
__libuc_fiber_spawn(size_t stack_length, int (*entry)(void *), void *argument);

/* Spawn on the running fiber's own scheduler and make the result
 * runnable; nullptr when the fiber cannot be created. */
[[nodiscard]] struct __libuc_fiber *
__libuc_fiber_start(size_t stack_length, int (*entry)(void *),
                    void *argument);

[[nodiscard]] bool __libuc_fiber_destroy(const struct __libuc_fiber *fiber);

/* NONE returned is a broken transfer; EXIT poisons the context, so
 * resuming again faults. */
[[nodiscard]] enum __libuc_fiber_request
__libuc_fiber_resume(struct __libuc_fiber *fiber);

/* Suspend the running fiber back to its resumer; with no current fiber
 * this faults. */
void __libuc_fiber_yield(void);

/* Exactly one CQE; the reap loop traps streams. The suspended frame
 * keeps the SQE and its buffers live through the CQE. Returns res; with
 * no current fiber this faults. */
[[nodiscard]] long __libuc_fiber_await(const struct io_uring_sqe *sqe);

/* The running fiber; nullptr where no block is installed. */
[[nodiscard]] struct __libuc_fiber *__libuc_fiber_current(void);

#endif
