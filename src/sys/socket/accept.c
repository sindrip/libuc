#include <stdint.h>

#include <linux/io_uring.h>

#include <sys/socket.h>

#include "errno/result.h"
#include "fiber/fiber.h"

int accept(int fd, struct sockaddr *addr, socklen_t *len) {
  const struct io_uring_sqe sqe = {
      .opcode = IORING_OP_ACCEPT,
      .fd = fd,
      .addr = (uintptr_t)addr,
      .addr2 = (uintptr_t)len,
  };

  return (int)__libuc_errno_result(__libuc_fiber_await(&sqe));
}
