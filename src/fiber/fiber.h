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
};

struct __libuc_fiber {
  /* where the fiber stopped */
  struct fiber_context context;
  void (*entry)(void *);
  void *argument;
  unsigned char *stack;
  size_t stack_length;
  struct __libuc_thread_local_block thread_local_block;
  /* The pending resume's frame; dead between resumes. */
  struct fiber_context *return_to;
  /* Scheduler-owned FIFO link; a fiber is in at most one queue. */
  struct __libuc_fiber *ready_next;
  /* The parked frame's SQE; the AWAIT arm consumes it before any resume. */
  const struct io_uring_sqe *await_sqe;
  /* The completion's res; the reactor stores it before the wake. */
  long await_res;
  enum __libuc_fiber_request request;
};

/* Create a fiber whose record lives at the top of its own stack mapping,
 * so the stack grows down away from it. nullptr when the mapping fails or
 * the stack cannot hold the record; destroying the fiber invalidates the
 * pointer along with the mapping. */
[[nodiscard]] struct __libuc_fiber *
__libuc_fiber_spawn(size_t stack_length, void (*entry)(void *), void *argument);

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
