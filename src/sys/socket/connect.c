#include <stdint.h>

#include <linux/io_uring.h>

#include <sys/socket.h>

#include "errno/result.h"
#include "fiber/fiber.h"

int connect(int fd, const struct sockaddr *addr, socklen_t len) {
  /* Special case: prep snapshots the sockaddr (move_addr_to_kernel), so
   * the kernel is done with it at submission, not the CQE. */
  const struct io_uring_sqe sqe = {
      .opcode = IORING_OP_CONNECT,
      .fd = fd,
      .addr = (uintptr_t)addr,
      .addr2 = len,
  };

  return (int)__libuc_errno_result(__libuc_fiber_await(&sqe));
}
