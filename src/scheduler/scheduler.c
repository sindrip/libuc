#include "scheduler.h"

#include <stdint.h>

/* Bounds one submission batch, nothing else: a larger batch enters twice.
 * Untuned until a measurement exists. */
constexpr uint32_t scheduler_ring_entries = 64;

[[nodiscard]] bool
__libuc_scheduler_become(struct __libuc_scheduler *scheduler) {
  *scheduler = (struct __libuc_scheduler){
      .ready_head = nullptr,
      .ready_tail = nullptr,
  };

  return __libuc_ring_create(&scheduler->ring, scheduler_ring_entries);
}
