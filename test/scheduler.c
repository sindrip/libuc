#include "scheduler/scheduler.h"

#include <string.h>

#include "fiber/fiber.h"
#include "thread_local/thread_local.h"

static _Thread_local int value;

static char order[12];
static size_t order_count;
static bool turns_ok = true;

struct actor {
  struct __libuc_fiber *fiber;
  unsigned long label;
};

static int entry(void *opaque) {
  struct actor *actor = opaque;

  for (int turn = 0; turn < 3; turn++) {
    turns_ok =
        turns_ok && __libuc_fiber_current() == actor->fiber && value == turn;

    if (order_count < sizeof(order)) {
      order[order_count] = (char)actor->label;
      order[order_count + 1] = (char)('0' + turn);
    }
    order_count += 2;

    value++;
    if (turn < 2) {
      __libuc_fiber_yield();
    }
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

  struct actor first = {.label = 'A'};
  struct actor second = {.label = 'B'};
  if ((first.fiber = __libuc_fiber_spawn((size_t)256 * 1024, entry, &first)) ==
          nullptr ||
      (second.fiber = __libuc_fiber_spawn((size_t)256 * 1024, entry,
                                          &second)) == nullptr) {
    return 123;
  }

  __libuc_scheduler_enqueue(&scheduler, first.fiber);
  __libuc_scheduler_enqueue(&scheduler, second.fiber);
  if (__libuc_scheduler_run(&scheduler) != 0) {
    __builtin_trap();
  }

  if (order_count != sizeof(order) ||
      memcmp(order, "A0B0A1B1A2B2", sizeof(order)) != 0) {
    return 122;
  }
  if (!turns_ok) {
    return 121;
  }

  if (!__libuc_fiber_destroy(first.fiber) ||
      !__libuc_fiber_destroy(second.fiber)) {
    return 120;
  }
  return 0;
}
