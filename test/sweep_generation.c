#include <linux/io_uring.h>

#include "fiber/fiber.h"
#include "scheduler/scheduler.h"
#include "thread_local/thread_local.h"

/* The starvation shape UC-015 exists for: one fiber parked on the ring
 * while another yields persistently. The limit only bounds the failure
 * mode; the pass requires the wake on the spinner's third turn. */
constexpr int turn_limit = 5;

static bool woken;
static int turns_until_wake = -1;

static int parker([[maybe_unused]] void *opaque) {
  struct io_uring_sqe nop = {.opcode = IORING_OP_NOP};
  woken = __libuc_fiber_await(&nop) == 0;

  return 0;
}

static int spinner([[maybe_unused]] void *opaque) {
  for (int turn = 0; turn < turn_limit; turn++) {
    if (woken) {
      turns_until_wake = turn;
      return 0;
    }
    __libuc_fiber_yield();
  }

  return 0;
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

  struct __libuc_fiber *parks;
  struct __libuc_fiber *spins;
  if ((parks = __libuc_fiber_spawn((size_t)64 * 1024, parker, nullptr)) ==
          nullptr ||
      (spins = __libuc_fiber_spawn((size_t)64 * 1024, spinner, nullptr)) ==
          nullptr) {
    return 123;
  }

  __libuc_scheduler_enqueue(&scheduler, parks);
  __libuc_scheduler_enqueue(&scheduler, spins);
  if (__libuc_scheduler_run(&scheduler) != 0) {
    __builtin_trap();
  }

  if (!woken) {
    return 122;
  }
  if (turns_until_wake != 2) {
    return 121;
  }
  if (scheduler.parked_count != 0 || scheduler.ready_count != 0 ||
      scheduler.ready_head != nullptr) {
    return 120;
  }

  if (!__libuc_fiber_destroy(parks) || !__libuc_fiber_destroy(spins)) {
    return 119;
  }
  return 0;
}
