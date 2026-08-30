#include <linux/io_uring.h>

#include <unistd.h>

#include "errno/result.h"
#include "fiber/fiber.h"

int close(int fd) {
  const struct io_uring_sqe sqe = {
      .opcode = IORING_OP_CLOSE,
      .fd = fd,
  };

  return (int)__libuc_errno_result(__libuc_fiber_await(&sqe));
}
