#include <threads.h>

static int worker([[maybe_unused]] void *opaque) { return 0; }

int main(void) {
  thrd_t thread;
  if (thrd_create(&thread, worker, nullptr) != thrd_success) {
    return 99;
  }

  /* Only main's thread ends; the worker runs on, and the program exits
   * EXIT_SUCCESS after the last thread (C11 7.26.5.5), not with 7. */
  thrd_exit(7);
}
