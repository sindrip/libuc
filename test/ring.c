#include <linux/io_uring.h>

#include "ring/ring.h"
#include "syscall.h"

int main(void) {
  /* 125 singles out a kernel refusing the required setup flags. */
  struct __libuc_ring ring;
  if (!__libuc_ring_create(&ring, 8)) {
    return 125;
  }

  struct io_uring_sqe *sqe = __libuc_ring_append_sqe(&ring);
  if (sqe == nullptr) {
    return 124;
  }
  sqe->opcode = IORING_OP_NOP;
  sqe->user_data = 0x0123456789abcdef;

  const long submitted = __libuc_ring_submit(&ring, 1);
  if (__libuc_syscall_failed(submitted) || submitted != 1) {
    return 123;
  }

  struct io_uring_cqe completion;
  if (!__libuc_ring_reap(&ring, &completion)) {
    return 122;
  }
  if (completion.res != 0 || completion.user_data != 0x0123456789abcdef) {
    return 121;
  }

  /* Exactly one. */
  if (__libuc_ring_reap(&ring, &completion)) {
    return 120;
  }
  return 0;
}
