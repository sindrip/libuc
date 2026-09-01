#include <uc/fiber.h>

#include <stdint.h>

#include <linux/io_uring.h>

#include "ring.h"
#include "syscall.h"

struct completion {
  int32_t result;
  uint32_t flags;
};

struct scheduler {
  struct __libuc_scheduler_ring ring;
};

struct scheduled_fiber {
  struct uc_fiber *fiber;
};

enum operation_state : uint8_t {
  OPERATION_PREPARED,
  OPERATION_IN_FLIGHT,
  OPERATION_COMPLETE,
};

/* The operation, unlike an individual REQUEST_NEXT, remains stable for the
 * complete kernel request lifetime. CQE user_data identifies this record; the
 * reactor writes one completion and resumes its owner. */
struct operation {
  struct scheduled_fiber *owner;
  struct io_uring_sqe submission;
  struct completion completion;
  enum operation_state state;
};

enum request_kind : uint8_t {
  REQUEST_YIELD,
  REQUEST_NEXT,
};

/* This record remains on the suspended fiber's stack. REQUEST_NEXT names the
 * fiber's one current logical operation. */
struct request {
  enum request_kind kind;
  struct operation *operation;
};

static void handle_next(struct scheduler *scheduler,
                        struct scheduled_fiber *scheduled,
                        struct operation *operation) {
  if (operation->state != OPERATION_PREPARED) {
    return;
  }

  struct io_uring_sqe *submission =
      __libuc_scheduler_ring_append(&scheduler->ring);
  if (submission == nullptr) {
    if (__libuc_syscall_failed(
            __libuc_scheduler_ring_submit(&scheduler->ring, 0))) {
      __builtin_trap();
    }

    submission = __libuc_scheduler_ring_append(&scheduler->ring);
    if (submission == nullptr) {
      __builtin_trap();
    }
  }

  operation->owner = scheduled;
  *submission = operation->submission;
  submission->user_data = (uintptr_t)operation;
  operation->state = OPERATION_IN_FLIGHT;
}

/* The scheduler owns all queueing and io_uring state. It observes fibers only
 * through their public transfer API. Operation adapters prepare SQEs and own
 * result and cancellation policy. */
[[maybe_unused]] static void dispatch(struct scheduler *scheduler,
                                      struct scheduled_fiber *scheduled,
                                      void *value) {
  const struct uc_fiber_result result =
      uc_fiber_resume(scheduled->fiber, value);

  switch (result.kind) {
  case UC_FIBER_RETURNED:
    /* The fiber finished with result.value. */
    break;

  case UC_FIBER_SUSPENDED: {
    struct request *request = result.value;

    switch (request->kind) {
    case REQUEST_YIELD:
      /* Put scheduled back on the ready queue. */
      break;

    case REQUEST_NEXT:
      handle_next(scheduler, scheduled, request->operation);
      break;
    }
    break;
  }
  }
}

[[maybe_unused]] static void
handle_completion(struct scheduler *scheduler,
                  const struct io_uring_cqe *completion) {
  struct operation *operation =
      (struct operation *)(uintptr_t)completion->user_data;

  operation->completion = (struct completion){
      .result = completion->res,
      .flags = completion->flags,
  };

  if (!(completion->flags & IORING_CQE_F_MORE)) {
    operation->state = OPERATION_COMPLETE;
  }

  dispatch(scheduler, operation->owner, &operation->completion);
}
