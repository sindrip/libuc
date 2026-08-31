#include <stddef.h>

#include <threads.h>

#include "fiber/fiber.h"
#include "scheduler/scheduler.h"
#include "thread_local/thread_local.h"

static thrd_t main_thread;
static thrd_t worker_thread;
static bool worker_ran;
static bool worker_ok = true;
static size_t worker_turns;
static size_t creator_turns;
static int worker_result;
static int quick_result;

/* thrd_exit ends the thread from any call depth with that result,
 * exactly as returning it from the entry does. */
[[noreturn]] static void finish(int res) { thrd_exit(res); }

static int worker(void *opaque) {
  worker_ran = true;
  worker_ok = worker_ok && opaque == &main_thread;

  /* thrd_create's completion synchronizes with this thread's start
   * (C11 7.26.5.1), so the handle its creator stored is readable here
   * and names this thread. */
  worker_ok = worker_ok && thrd_equal(thrd_current(), worker_thread);
  worker_ok = worker_ok && !thrd_equal(thrd_current(), main_thread);

  worker_turns++;
  thrd_yield();
  worker_turns++;

  finish(7);
}

static int quick([[maybe_unused]] void *opaque) { return 9; }

static int creator([[maybe_unused]] void *opaque) {
  main_thread = thrd_current();
  worker_ok = worker_ok && thrd_equal(main_thread, thrd_current());

  if (thrd_create(&worker_thread, worker, &main_thread) != thrd_success) {
    return 1;
  }

  creator_turns++;
  /* The worker cannot have run yet, so this join blocks until it
   * exits. */
  if (thrd_join(worker_thread, &worker_result) != thrd_success) {
    return 2;
  }

  thrd_t quick_thread;
  if (thrd_create(&quick_thread, quick, nullptr) != thrd_success) {
    return 3;
  }
  thrd_yield();
  /* The yield let quick run to exit, so this join takes a zombie
   * without blocking. */
  if (thrd_join(quick_thread, &quick_result) != thrd_success) {
    return 4;
  }

  creator_turns++;
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

  struct __libuc_fiber *first =
      __libuc_fiber_spawn((size_t)256 * 1024, creator, nullptr);
  if (first == nullptr) {
    return 123;
  }

  __libuc_scheduler_enqueue(&scheduler, first);
  __libuc_scheduler_run(&scheduler);

  if (!worker_ran || !worker_ok) {
    return 122;
  }
  /* Interleaving after a spawn is the scheduler's business, not a
   * promise; that yield resumes a fiber where it left off is fiber_io's
   * contract. Here: both threads ran to completion. */
  if (worker_turns != 2 || creator_turns != 2) {
    return 121;
  }
  /* Each join took its target's result and released its record; the
   * handles are dead, so only the never-joined creator remains. */
  if (worker_result != 7 || quick_result != 9) {
    return 120;
  }
  if (first->status != 0) {
    return 119;
  }

  if (!__libuc_fiber_destroy(first)) {
    return 118;
  }

  return 0;
}
