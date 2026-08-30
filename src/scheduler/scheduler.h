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

#endif
