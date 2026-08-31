#ifndef LIBUC_SRC_SCHEDULER_SCHEDULER_H
#define LIBUC_SRC_SCHEDULER_SCHEDULER_H

#include "fiber/fiber.h"
#include "ring/ring.h"

/* SQ entries; a power of two is granted exactly by io_uring_setup, up to
 * the kernel's IORING_MAX_ENTRIES. */
constexpr uint32_t __libuc_scheduler_ring_entries = 1U << 10;
static_assert(__libuc_scheduler_ring_entries == 1024);

struct __libuc_scheduler {
  struct __libuc_ring ring;
  struct __libuc_fiber *ready_head;
  struct __libuc_fiber *ready_tail;
  uint32_t ready_count;
  uint32_t parked_count;
};

[[nodiscard]] bool
__libuc_scheduler_become(struct __libuc_scheduler *scheduler);

void __libuc_scheduler_enqueue(struct __libuc_scheduler *scheduler,
                               struct __libuc_fiber *fiber);

void __libuc_scheduler_run(struct __libuc_scheduler *scheduler);

#endif
