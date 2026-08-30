#ifndef LIBUC_SRC_ERRNO_RESULT_H
#define LIBUC_SRC_ERRNO_RESULT_H

#include <errno.h>

static inline long __libuc_errno_result(long res) {
  if (res < 0) {
    errno = (int)-res;
    return -1;
  }

  return res;
}

#endif
