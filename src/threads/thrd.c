#include <threads.h>

#include "fiber/fiber.h"

/* C11 leaves the stack size to the implementation and gives no way to
 * ask for one; untuned until a measurement exists. */
constexpr size_t thread_stack_length = (size_t)256 * 1024;

int thrd_create(thrd_t *thr, thrd_start_t func, void *arg) {
  thrd_t thread = __libuc_fiber_start(thread_stack_length, func, arg);
  if (thread == nullptr) {
    return thrd_nomem;
  }

  *thr = thread;
  return thrd_success;
}

thrd_t thrd_current(void) { return __libuc_fiber_current(); }

int thrd_detach(thrd_t thr) {
  __libuc_fiber_detach(thr);
  return thrd_success;
}

int thrd_equal(thrd_t lhs, thrd_t rhs) { return lhs == rhs; }

[[noreturn]] void thrd_exit(int res) { __libuc_fiber_exit(res); }

int thrd_join(thrd_t thr, int *res) {
  const long status = __libuc_fiber_join(thr);
  if (res != nullptr) {
    *res = (int)status;
  }
  return thrd_success;
}

void thrd_yield(void) { __libuc_fiber_yield(); }
