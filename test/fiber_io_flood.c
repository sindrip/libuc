#include <stddef.h>

#include <linux/io_uring.h>

#include "fiber/fiber.h"
#include "scheduler/scheduler.h"
#include "thread_local/thread_local.h"

/* One sweep parks more fibers than the ring has SQ entries, so the batch
 * must flush mid-sweep or the rest are silently dropped. */
constexpr size_t flood_count = __libuc_scheduler_ring_entries + 6;

static struct __libuc_fiber fibers[flood_count];
static size_t wakes;
static bool wakes_ok = true;

static void parker([[maybe_unused]] void *opaque) {
  struct io_uring_sqe nop = {.opcode = IORING_OP_NOP};
  wakes_ok = wakes_ok && __libuc_fiber_await(&nop) == 0;
  wakes++;
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

  for (size_t i = 0; i < flood_count; i++) {
    if (!__libuc_fiber_create(&fibers[i], (size_t)64 * 1024, parker,
                              nullptr)) {
      return 123;
    }
  }

  for (size_t i = 0; i < flood_count; i++) {
    __libuc_scheduler_enqueue(&scheduler, &fibers[i]);
  }
  __libuc_scheduler_run(&scheduler);

  if (wakes != flood_count) {
    return 122;
  }
  if (!wakes_ok) {
    return 121;
  }
  if (scheduler.parked != 0 || scheduler.ready != 0 ||
      scheduler.ready_head != nullptr) {
    return 120;
  }

  for (size_t i = 0; i < flood_count; i++) {
    if (!__libuc_fiber_destroy(&fibers[i])) {
      return 119;
    }
  }
  return 0;
}
