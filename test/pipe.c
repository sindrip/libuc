#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "fiber/fiber.h"
#include "scheduler/scheduler.h"
#include "thread_local/thread_local.h"

static bool piper_ok = true;

static void check(bool ok) { piper_ok = piper_ok && ok; }

static void piper([[maybe_unused]] void *opaque) {
  errno = 11;

  int plain[2] = {-1, -1};
  check(pipe(plain) == 0);
  check(plain[0] >= 0 && plain[1] >= 0 && plain[0] != plain[1]);

  int cloexec[2] = {-1, -1};
  check(pipe2(cloexec, O_CLOEXEC) == 0);
  check(cloexec[0] >= 0 && cloexec[1] >= 0);

  check(errno == 11);

  int untouched[2] = {-1, -1};
  check(pipe2(untouched, 1) == -1);
  check(errno == EINVAL);
  check(untouched[0] == -1 && untouched[1] == -1);

  check(close(plain[0]) == 0 && close(plain[1]) == 0);
  check(close(cloexec[0]) == 0 && close(cloexec[1]) == 0);
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

  struct __libuc_fiber *fiber;
  if ((fiber = __libuc_fiber_spawn((size_t)256 * 1024, piper, nullptr)) ==
      nullptr) {
    return 123;
  }

  __libuc_scheduler_enqueue(&scheduler, fiber);
  __libuc_scheduler_run(&scheduler);

  if (!piper_ok) {
    return 122;
  }
  if (scheduler.parked_count != 0 || scheduler.ready_count != 0 ||
      scheduler.ready_head != nullptr) {
    return 121;
  }
  if (!__libuc_fiber_destroy(fiber)) {
    return 120;
  }

  return 0;
}
