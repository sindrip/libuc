#include <stdint.h>

#include <linux/io_uring.h>

#include <sys/socket.h>

#include "errno/result.h"
#include "fiber/fiber.h"

int listen(int fd, int backlog) {
  const struct io_uring_sqe sqe = {
      .opcode = IORING_OP_LISTEN,
      .fd = fd,
      .len = (uint32_t)backlog,
  };

  return (int)__libuc_errno_result(__libuc_fiber_await(&sqe));
}
