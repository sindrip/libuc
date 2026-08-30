#ifndef LIBUC_SRC_FIBER_FIBER_H
#define LIBUC_SRC_FIBER_FIBER_H

#include <stddef.h>

#include "fiber_arch.h"
#include "thread_local/thread_local.h"

struct __libuc_fiber {
  struct fiber_context context; /* where the fiber stopped */
  void (*entry)(void *);
  void *argument;
  unsigned char *stack;
  size_t stack_length;
  struct __libuc_thread_local_block thread_local_block;
  /* Where completion lands: the pending run's own frame. Set by run, dead
   * between runs. */
  struct fiber_context *return_to;
};

[[nodiscard]] bool __libuc_fiber_create(struct __libuc_fiber *fiber,
                                        size_t stack_length,
                                        void (*entry)(void *), void *argument);

[[nodiscard]] bool __libuc_fiber_destroy(const struct __libuc_fiber *fiber);

/* Run the fiber's entry to completion on the fiber's stack. A fiber runs
 * exactly once: completion poisons the context, and a second run faults on
 * its first instruction. */
void __libuc_fiber_run(struct __libuc_fiber *fiber);

/* The fiber whose thread-local block is installed, read through its TCB;
 * nullptr on the bootstrap context, whose thread pointer stays absent. */
[[nodiscard]] struct __libuc_fiber *__libuc_fiber_current(void);

#endif
