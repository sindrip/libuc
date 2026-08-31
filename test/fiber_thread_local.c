#include "fiber/fiber.h"
#include "thread_local/thread_local.h"

static _Thread_local int value = 7;

struct report {
  struct __libuc_fiber *fiber;
  int write;
  int ok;
};

static int entry(void *opaque) {
  struct report *report = opaque;
  int local = 1;

  report->ok = __libuc_fiber_current() == report->fiber && value == 7;
  value = report->write;
  __libuc_fiber_yield();

  report->ok = report->ok && __libuc_fiber_current() == report->fiber &&
               value == report->write && local == 1;
  value = report->write + 1;
  local = 2;
  __libuc_fiber_yield();

  report->ok = report->ok && value == report->write + 1 && local == 2;

  return 0;
}

int main(void) {
  /* 125 singles out an environment where user mode cannot install. */
  if (!__libuc_thread_local_install_available()) {
    return 125;
  }

  struct __libuc_fiber *root = __libuc_fiber_current();
  if (root == nullptr) {
    return 124;
  }

  /* A fiber's identity exists only once it is spawned, so the reports
   * learn their fiber after the fact. */
  struct report first_report = {.write = 11, .ok = 0};
  struct report second_report = {.write = 22, .ok = 0};
  struct __libuc_fiber *first =
      __libuc_fiber_spawn((size_t)256 * 1024, entry, &first_report);
  struct __libuc_fiber *second =
      __libuc_fiber_spawn((size_t)256 * 1024, entry, &second_report);
  if (first == nullptr || second == nullptr) {
    return 123;
  }
  first_report.fiber = first;
  second_report.fiber = second;

  /* A suspended fiber's thread-local value must survive the sibling's
   * turns. */
  for (int turn = 0; turn < 2; turn++) {
    if (__libuc_fiber_resume(first)->kind != __LIBUC_FIBER_REQUEST_YIELD ||
        __libuc_fiber_current() != root ||
        __libuc_fiber_resume(second)->kind != __LIBUC_FIBER_REQUEST_YIELD) {
      return 122;
    }
  }
  if (__libuc_fiber_resume(first)->kind != __LIBUC_FIBER_REQUEST_EXIT ||
      __libuc_fiber_resume(second)->kind != __LIBUC_FIBER_REQUEST_EXIT ||
      __libuc_fiber_current() != root) {
    return 121;
  }

  if (!first_report.ok || !second_report.ok) {
    return 120;
  }

  if (!__libuc_fiber_destroy(first) || !__libuc_fiber_destroy(second)) {
    return 119;
  }
  return 0;
}
