#include <errno.h>
#include <sys/auxv.h>

#include "thread_local/thread_local.h"

int main(void) {
  /* 125 singles out an environment where user mode cannot install. */
  if (!__libuc_thread_local_install_available()) {
    return 125;
  }

  errno = 11;

  if (getauxval(AT_PAGESZ) != 4096) {
    return 124;
  }
  if (errno != 11) {
    return 123;
  }

  if (getauxval(~0UL) != 0) {
    return 122;
  }
  if (errno != ENOENT) {
    return 121;
  }

  return 0;
}
