#include <stdint.h>

#include <linux/io_uring.h>

#include <sys/socket.h>

#include "errno/result.h"
#include "fiber/fiber.h"

int socket(int domain, int type, int protocol) {
  const struct io_uring_sqe sqe = {
      .opcode = IORING_OP_SOCKET,
      .fd = domain,
      .off = (uint32_t)type,
      .len = (uint32_t)protocol,
  };

  return (int)__libuc_errno_result(__libuc_fiber_await(&sqe));
}
