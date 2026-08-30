#include "thread_local/thread_local.h"

static _Thread_local int value = 7;

/* The accessors stay out of line: within one frame the compiler may reuse a
 * TP-relative address across calls, and this probe changes TP between them. */
[[gnu::noinline]] static void set_value(int replacement) {
  value = replacement;
}

[[gnu::noinline]] static int get_value(void) {
  return value;
}

int main(void) {
  /* 125 singles out an environment where user mode cannot install. */
  if (!__libuc_thread_local_install_available()) {
    return 125;
  }

  struct __libuc_thread_local_block first;
  struct __libuc_thread_local_block second;
  if (!__libuc_thread_local_block_create(&first) ||
      !__libuc_thread_local_block_create(&second)) {
    return 124;
  }

  __libuc_thread_local_block_install(&first);
  set_value(11);

  __libuc_thread_local_block_install(&second);
  if (get_value() != 7) {
    return 123;
  }
  set_value(22);

  __libuc_thread_local_block_install(&first);
  if (get_value() != 11) {
    return 122;
  }

  __libuc_thread_local_block_install(&second);
  if (get_value() != 22) {
    return 121;
  }

  if (!__libuc_thread_local_block_destroy(&first) ||
      !__libuc_thread_local_block_destroy(&second)) {
    return 120;
  }
  return 0;
}
