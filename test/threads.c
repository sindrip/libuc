#include <stddef.h>
#include <stdint.h>

#include <threads.h>

static thrd_t main_thread;
static thrd_t worker_thread;
static bool worker_ran;
static bool worker_ok = true;
static size_t worker_turns;
static int worker_result;
static int quick_result;
static bool detached_ran;

/* From a nested call, exactly as returning from the entry. */
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

static int detached([[maybe_unused]] void *opaque) {
  detached_ran = true;
  return 5;
}

/* Never exits: returning from main below abandons it mid-spin
 * (C11 5.1.2.2.3); a drain loop would run it forever. */
[[noreturn]] static int spinner([[maybe_unused]] void *opaque) {
  for (;;) {
    thrd_yield();
  }
}

int main(void) {
  main_thread = thrd_current();

  if (thrd_create(&worker_thread, worker, &main_thread) != thrd_success) {
    return 99;
  }
  /* The worker cannot have run yet: this join blocks. */
  if (thrd_join(worker_thread, &worker_result) != thrd_success) {
    return 98;
  }

  thrd_t quick_thread;
  if (thrd_create(&quick_thread, quick, nullptr) != thrd_success) {
    return 97;
  }
  thrd_yield();
  /* quick has exited: this join takes a zombie. */
  if (thrd_join(quick_thread, &quick_result) != thrd_success) {
    return 96;
  }

  thrd_t detached_thread;
  if (thrd_create(&detached_thread, detached, nullptr) != thrd_success) {
    return 95;
  }
  if (thrd_detach(detached_thread) != thrd_success) {
    return 94;
  }
  thrd_yield();
  /* The detached thread exited and was reclaimed with no join. */
  if (!detached_ran) {
    return 93;
  }

  thrd_t zombie_thread;
  if (thrd_create(&zombie_thread, quick, nullptr) != thrd_success) {
    return 92;
  }
  const uintptr_t zombie_address = (uintptr_t)zombie_thread;
  thrd_yield();
  /* Already a zombie: this detach disposes of it now. */
  if (thrd_detach(zombie_thread) != thrd_success) {
    return 91;
  }

  /* Every thread stack is the same size and this kernel's mmap fills
   * the highest fitting hole, so a fully reclaimed chain hands the same
   * slot back; a leaked mapping shifts the address. */
  thrd_t reuse_thread;
  if (thrd_create(&reuse_thread, quick, nullptr) != thrd_success) {
    return 90;
  }
  if ((uintptr_t)reuse_thread != zombie_address) {
    return 89;
  }
  if (thrd_join(reuse_thread, nullptr) != thrd_success) {
    return 88;
  }

  if (!worker_ran || !worker_ok || worker_turns != 2) {
    return 87;
  }
  if (worker_result != 7 || quick_result != 9) {
    return 86;
  }

  thrd_t spin_thread;
  if (thrd_create(&spin_thread, spinner, nullptr) != thrd_success) {
    return 85;
  }

  return 0;
}
