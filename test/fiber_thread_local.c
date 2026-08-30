#include "fiber/fiber.h"
#include "thread_local/thread_local.h"

static _Thread_local int value = 7;

struct report {
  struct __libuc_fiber *fiber;
  int write;
  int ok;
};

static void entry(void *opaque) {
  struct report *report = opaque;
  report->ok = __libuc_fiber_current() == report->fiber && value == 7;

  value = report->write;
  report->ok = report->ok && value == report->write;
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

  struct __libuc_fiber first;
  struct __libuc_fiber second;
  struct report first_report = {.fiber = &first, .write = 11, .ok = 0};
  struct report second_report = {.fiber = &second, .write = 22, .ok = 0};
  if (!__libuc_fiber_create(&first, (size_t)256 * 1024, entry, &first_report) ||
      !__libuc_fiber_create(&second, (size_t)256 * 1024, entry,
                            &second_report)) {
    return 123;
  }

  const auto first_tcb = (const struct __libuc_thread_local_tcb *)
                             first.thread_local_block.thread_pointer;
  const auto second_tcb = (const struct __libuc_thread_local_tcb *)
                              second.thread_local_block.thread_pointer;
  if (first_tcb->fiber != &first || second_tcb->fiber != &second) {
    return 122;
  }

  __libuc_fiber_run(&first);
  if (__libuc_fiber_current() != root) {
    return 121;
  }
  __libuc_fiber_run(&second);
  if (__libuc_fiber_current() != root) {
    return 121;
  }

  /* second saw 7, not first's 11: the blocks are per fiber. */
  if (!first_report.ok || !second_report.ok) {
    return 120;
  }

  if (!__libuc_fiber_destroy(&first) || !__libuc_fiber_destroy(&second)) {
    return 119;
  }
  return 0;
}
