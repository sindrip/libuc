#include <errno.h>
#include <unistd.h>

#include "fiber/fiber.h"
#include "scheduler/scheduler.h"
#include "thread_local/thread_local.h"

static bool closer_ok = true;

static void closer([[maybe_unused]] void *opaque) {
  errno = 11;

  closer_ok = closer_ok && close(0) == 0;
  closer_ok = closer_ok && errno == 11;

  const int again = close(0);
  closer_ok = closer_ok && again == -1 && errno == EBADF;
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

  struct __libuc_fiber fiber;
  if (!__libuc_fiber_create(&fiber, (size_t)256 * 1024, closer, nullptr)) {
    return 123;
  }

  __libuc_scheduler_enqueue(&scheduler, &fiber);
  __libuc_scheduler_run(&scheduler);

  if (!closer_ok) {
    return 122;
  }
  if (scheduler.parked_count != 0 || scheduler.ready_count != 0 ||
      scheduler.ready_head != nullptr) {
    return 121;
  }
  if (!__libuc_fiber_destroy(&fiber)) {
    return 120;
  }

  return 0;
}
