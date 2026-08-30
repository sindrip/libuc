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

void __libuc_scheduler_enqueue(struct __libuc_scheduler *scheduler,
                               struct __libuc_fiber *fiber) {
  fiber->ready_next = nullptr;

  if (scheduler->ready_tail == nullptr) {
    scheduler->ready_head = fiber;
  } else {
    scheduler->ready_tail->ready_next = fiber;
  }
  scheduler->ready_tail = fiber;
}

void __libuc_scheduler_run(struct __libuc_scheduler *scheduler) {
  while (scheduler->ready_head != nullptr) {
    struct __libuc_fiber *fiber = scheduler->ready_head;
    scheduler->ready_head = fiber->ready_next;

    if (scheduler->ready_head == nullptr) {
      scheduler->ready_tail = nullptr;
    }
    fiber->ready_next = nullptr;

    switch (__libuc_fiber_resume(fiber)) {
    case __LIBUC_FIBER_REQUEST_YIELD:
      __libuc_scheduler_enqueue(scheduler, fiber);
      break;
    case __LIBUC_FIBER_REQUEST_EXIT:
      break;
    case __LIBUC_FIBER_REQUEST_NONE:
      __builtin_trap();
    }
  }
}
