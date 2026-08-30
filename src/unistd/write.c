#include <stdint.h>

#include <linux/io_uring.h>

#include <unistd.h>

#include "errno/result.h"
#include "fiber/fiber.h"
#include "rw_len.h"

ssize_t write(int fd, const void *buf, size_t count) {
  /* The kernel's equivalent check reads sqe.len and so cannot fire. */
  if ((ssize_t)count < 0) {
    errno = EINVAL;
    return -1;
  }

  const struct io_uring_sqe sqe = {
      .opcode = IORING_OP_WRITE,
      .fd = fd,
      .off = UINT64_MAX,
      .addr = (uintptr_t)buf,
      .len = __libuc_rw_len(count),
  };

  return __libuc_errno_result(__libuc_fiber_await(&sqe));
}
