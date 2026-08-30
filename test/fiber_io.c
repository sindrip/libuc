#include <string.h>

#include <linux/io_uring.h>

#include "fiber/fiber.h"
#include "scheduler/scheduler.h"
#include "thread_local/thread_local.h"

static char order[12];
static size_t order_count;
static bool wakes_ok = true;

struct actor {
  struct __libuc_fiber fiber;
  unsigned long label;
};

static void record(unsigned long label, int turn) {
  if (order_count < sizeof(order)) {
    order[order_count] = (char)label;
    order[order_count + 1] = (char)('0' + turn);
  }
  order_count += 2;
}

static void waiter(void *opaque) {
  struct actor *actor = opaque;

  for (int turn = 0; turn < 3; turn++) {
    record(actor->label, turn);

    struct io_uring_sqe nop = {.opcode = IORING_OP_NOP};
    wakes_ok = wakes_ok && __libuc_fiber_await(&nop) == 0;
  }
}

static void yielder(void *opaque) {
  struct actor *actor = opaque;

  for (int turn = 0; turn < 3; turn++) {
    record(actor->label, turn);
    if (turn < 2) {
      __libuc_fiber_yield();
    }
  }
}

int main(void) {
  /* 125 singles out an environment where user mode cannot install. */
  if (!__libuc_thread_local_install_available()) {
    return 125;
  }

  struct __libuc_scheduler scheduler;
  if (!__libuc_scheduler_become(&scheduler)) {
    return 124;
  }

  struct actor waits = {.label = 'A'};
  struct actor yields = {.label = 'B'};
  if (!__libuc_fiber_create(&waits.fiber, (size_t)256 * 1024, waiter,
                            &waits) ||
      !__libuc_fiber_create(&yields.fiber, (size_t)256 * 1024, yielder,
                            &yields)) {
    return 123;
  }

  __libuc_scheduler_enqueue(&scheduler, &waits.fiber);
  __libuc_scheduler_enqueue(&scheduler, &yields.fiber);
  __libuc_scheduler_run(&scheduler);

  if (order_count != sizeof(order) ||
      memcmp(order, "A0B0B1A1B2A2", sizeof(order)) != 0) {
    return 122;
  }
  if (!wakes_ok) {
    return 121;
  }
  if (scheduler.parked != 0 || scheduler.ready != 0 ||
      scheduler.ready_head != nullptr) {
    return 120;
  }

  if (!__libuc_fiber_destroy(&waits.fiber) ||
      !__libuc_fiber_destroy(&yields.fiber)) {
    return 119;
  }
  return 0;
}
