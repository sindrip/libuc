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

static int creator([[maybe_unused]] void *opaque) {
  main_thread = thrd_current();
  worker_ok = worker_ok && thrd_equal(main_thread, thrd_current());

  if (thrd_create(&worker_thread, worker, &main_thread) != thrd_success) {
    return 1;
  }

  creator_turns++;
  thrd_yield();
  creator_turns++;
  thrd_yield();

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
  if (first->status != 0 || worker_thread->status != 7) {
    return 120;
  }

  if (!__libuc_fiber_destroy(worker_thread) || !__libuc_fiber_destroy(first)) {
    return 119;
  }

  return 0;
}
