#ifndef LIBUC_SRC_SCHEDULER_SCHEDULER_H
#define LIBUC_SRC_SCHEDULER_SCHEDULER_H

#include "fiber/fiber.h"
#include "ring/ring.h"

struct __libuc_scheduler {
  struct __libuc_ring ring;
  struct __libuc_fiber *ready_head;
  struct __libuc_fiber *ready_tail;
};

[[nodiscard]] bool
__libuc_scheduler_become(struct __libuc_scheduler *scheduler);

void __libuc_scheduler_enqueue(struct __libuc_scheduler *scheduler,
                               struct __libuc_fiber *fiber);

/* Run until the ready queue empties. NONE from a resume is a broken
 * transfer and traps. */
void __libuc_scheduler_run(struct __libuc_scheduler *scheduler);

#endif
