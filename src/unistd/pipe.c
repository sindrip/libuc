#include <stdint.h>

#include <linux/io_uring.h>

#include <unistd.h>

#include "errno/result.h"
#include "fiber/fiber.h"

int pipe2(int fds[2], int flags) {
  const struct io_uring_sqe sqe = {
      .opcode = IORING_OP_PIPE,
      .addr = (uintptr_t)fds,
      .pipe_flags = (uint32_t)flags,
  };

  return (int)__libuc_errno_result(__libuc_fiber_await(&sqe));
}

int pipe(int fds[2]) { return pipe2(fds, 0); }
