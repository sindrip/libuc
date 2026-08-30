#ifndef LIBUC_SRC_FIBER_FIBER_H
#define LIBUC_SRC_FIBER_FIBER_H

#include <stddef.h>

#include "fiber_arch.h"
#include "thread_local/thread_local.h"

enum __libuc_fiber_request : unsigned long {
  __LIBUC_FIBER_REQUEST_NONE,
  __LIBUC_FIBER_REQUEST_YIELD,
  __LIBUC_FIBER_REQUEST_EXIT,
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
  enum __libuc_fiber_request request;
};

[[nodiscard]] bool __libuc_fiber_create(struct __libuc_fiber *fiber,
                                        size_t stack_length,
                                        void (*entry)(void *), void *argument);

[[nodiscard]] bool __libuc_fiber_destroy(const struct __libuc_fiber *fiber);

/* NONE returned is a broken transfer; EXIT poisons the context, so
 * resuming again faults. */
[[nodiscard]] enum __libuc_fiber_request
__libuc_fiber_resume(struct __libuc_fiber *fiber);

/* Suspend the running fiber back to its resumer; with no current fiber
 * this faults. */
void __libuc_fiber_yield(void);

/* The running fiber; nullptr where no block is installed. */
[[nodiscard]] struct __libuc_fiber *__libuc_fiber_current(void);

#endif
