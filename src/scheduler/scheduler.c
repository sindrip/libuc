#include "scheduler.h"

#include <stdint.h>

#include <linux/io_uring.h>

#include "syscall.h"

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
  scheduler->ready++;
  fiber->ready_next = nullptr;

  if (scheduler->ready_tail == nullptr) {
    scheduler->ready_head = fiber;
  } else {
    scheduler->ready_tail->ready_next = fiber;
  }
  scheduler->ready_tail = fiber;
}

static void park(struct __libuc_scheduler *scheduler,
                 struct __libuc_fiber *fiber) {
  /* IOSQE flags link batches or skip CQEs; none is designed. And a full
   * CQ lets the kernel drop wakes behind a masked -EBADR, so parking
   * stops at the CQ's capacity. */
  if (fiber->await_sqe->flags != 0 ||
      scheduler->parked == scheduler->ring.cq_entries) {
    __builtin_trap();
  }

  struct io_uring_sqe *slot = __libuc_ring_append_sqe(&scheduler->ring);
  if (slot == nullptr) {
    if (__libuc_syscall_failed(__libuc_ring_submit(&scheduler->ring, 0))) {
      __builtin_trap();
    }
    slot = __libuc_ring_append_sqe(&scheduler->ring);
    if (slot == nullptr) {
      __builtin_trap();
    }
  }

  *slot = *fiber->await_sqe;
  slot->user_data = (uintptr_t)fiber;
  scheduler->parked++;
}

void __libuc_scheduler_run(struct __libuc_scheduler *scheduler) {
  while (scheduler->parked + scheduler->ready != 0) {
    for (uint32_t turns = scheduler->ready; turns != 0; turns--) {
      struct __libuc_fiber *fiber = scheduler->ready_head;
      scheduler->ready_head = fiber->ready_next;
      scheduler->ready--;

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
      case __LIBUC_FIBER_REQUEST_AWAIT:
        park(scheduler, fiber);
        break;
      case __LIBUC_FIBER_REQUEST_NONE:
        __builtin_trap();
      }
    }

    if (scheduler->parked == 0) {
      continue;
    }

    const uint32_t min_complete = scheduler->ready == 0 ? 1 : 0;
    if (__libuc_syscall_failed(
            __libuc_ring_submit(&scheduler->ring, min_complete))) {
      __builtin_trap();
    }

    struct io_uring_cqe completion;
    while (__libuc_ring_reap(&scheduler->ring, &completion)) {
      /* Single-shot contract: every CQE is the sole completion of a parked
       * fiber's SQE. Streams and gap fillers trap until designed. */
      if (completion.flags &
          (IORING_CQE_F_MORE | IORING_CQE_F_NOTIF | IORING_CQE_F_SKIP)) {
        __builtin_trap();
      }

      struct __libuc_fiber *woken =
          (struct __libuc_fiber *)(uintptr_t)completion.user_data;

      woken->await_res = completion.res;
      scheduler->parked--;
      __libuc_scheduler_enqueue(scheduler, woken);
    }
  }
}
