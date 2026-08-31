#include "scheduler.h"

#include <stdint.h>

#include <linux/io_uring.h>

#include "syscall.h"

[[nodiscard]] bool
__libuc_scheduler_become(struct __libuc_scheduler *scheduler) {
  *scheduler = (struct __libuc_scheduler){
      .ready_head = nullptr,
      .ready_tail = nullptr,
  };

  return __libuc_ring_create(&scheduler->ring, __libuc_scheduler_ring_entries);
}

void __libuc_scheduler_enqueue(struct __libuc_scheduler *scheduler,
                               struct __libuc_fiber *fiber) {
  scheduler->ready_count++;
  fiber->ready_next = nullptr;

  if (scheduler->ready_tail == nullptr) {
    scheduler->ready_head = fiber;
  } else {
    scheduler->ready_tail->ready_next = fiber;
  }
  scheduler->ready_tail = fiber;
}

static void park(struct __libuc_scheduler *scheduler,
                 struct __libuc_fiber_request *request) {
  /* IOSQE flags link batches or skip CQEs; none is designed. And with
   * one CQE per parked operation, admitting at most cq_entries of them
   * means the CQ never fills, so the kernel's overflow path stays out of
   * the picture entirely. */
  const struct io_uring_sqe *await = request->argument;
  if (await->flags != 0 ||
      scheduler->parked_count == scheduler->ring.cq_entries) {
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

  *slot = *await;
  /* The completion carries the request back, and the request knows whom
   * to resume. */
  slot->user_data = (uintptr_t)request;
  scheduler->parked_count++;
}

void __libuc_scheduler_run(struct __libuc_scheduler *scheduler) {
  while (scheduler->parked_count + scheduler->ready_count != 0) {
    for (uint32_t turns = scheduler->ready_count; turns != 0; turns--) {
      struct __libuc_fiber *fiber = scheduler->ready_head;
      scheduler->ready_head = fiber->ready_next;
      scheduler->ready_count--;
      if (scheduler->ready_head == nullptr) {
        scheduler->ready_tail = nullptr;
      }
      fiber->ready_next = nullptr;

      struct __libuc_fiber_request *request = __libuc_fiber_resume(fiber);
      switch (request->kind) {
      case __LIBUC_FIBER_REQUEST_YIELD:
        __libuc_scheduler_enqueue(scheduler, fiber);
        break;
      case __LIBUC_FIBER_REQUEST_EXIT: {
        struct __libuc_fiber_request *joiner = fiber->joiner;
        if (joiner == nullptr) {
          break;
        }
        scheduler->joining_count--;
        joiner->result = fiber->status;
        /* The EXIT request lives on the mapping being released; neither
         * is touched past this point. */
        if (!__libuc_fiber_destroy(fiber)) {
          __builtin_trap();
        }
        __libuc_scheduler_enqueue(scheduler, joiner->fiber);
        break;
      }
      case __LIBUC_FIBER_REQUEST_AWAIT:
        park(scheduler, request);
        break;
      case __LIBUC_FIBER_REQUEST_SPAWN: {
        const struct __libuc_fiber_spawn_request *spawn = request->argument;
        struct __libuc_fiber *spawned = __libuc_fiber_spawn(
            spawn->stack_length, spawn->entry, spawn->argument);

        /* The spawner goes back on the queue ahead of what it made:
         * thrd_create's completion synchronizes with the start of the new
         * thread (C11 7.26.5.1), so the handle must be stored before the
         * child can read it. */
        request->result = (long)(uintptr_t)spawned;
        __libuc_scheduler_enqueue(scheduler, fiber);
        if (spawned != nullptr) {
          __libuc_scheduler_enqueue(scheduler, spawned);
        }
        break;
      }
      case __LIBUC_FIBER_REQUEST_JOIN: {
        struct __libuc_fiber *target = request->target;
        if (target == fiber || target->joiner != nullptr) {
          __builtin_trap();
        }
        if (target->life == __LIBUC_FIBER_EXITED) {
          request->result = target->status;
          if (!__libuc_fiber_destroy(target)) {
            __builtin_trap();
          }
          __libuc_scheduler_enqueue(scheduler, fiber);
          break;
        }
        target->joiner = request;
        scheduler->joining_count++;
        break;
      }
      }
    }

    if (scheduler->parked_count == 0) {
      continue;
    }

    const uint32_t min_complete = scheduler->ready_count == 0 ? 1 : 0;
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

      struct __libuc_fiber_request *answered =
          (struct __libuc_fiber_request *)(uintptr_t)completion.user_data;
      answered->result = completion.res;
      struct __libuc_fiber *woken = answered->fiber;

      scheduler->parked_count--;
      __libuc_scheduler_enqueue(scheduler, woken);
    }
  }

  /* Joiners left with nothing runnable and nothing in flight are a
   * cycle: a program bug, not a wait. */
  if (scheduler->joining_count != 0) {
    __builtin_trap();
  }
}
