/*
 * Operation descriptions. Contracts are in io.h.
 *
 * This file contains no mechanism, and that is a property worth checking
 * rather than a claim: no rt_switch, no rt_fiber_suspend, no rt_fiber_current,
 * no ring. Every function is a struct literal and a call to
 * rt_fiber_await_io — which means the audit surface for "what addresses does
 * the runtime hand the kernel" is exactly the initializers below plus the one
 * function they all call.
 */

#include "io.h"

#include <stdint.h>

#include <linux/io_uring.h>

#include "fiber.h"

int rt_nop(void) {
  return rt_fiber_await_io((struct io_uring_sqe){
      .opcode = IORING_OP_NOP,
  });
}

int rt_write(int fd, const void *buf, unsigned len) {
  return rt_fiber_await_io((struct io_uring_sqe){
      .opcode = IORING_OP_WRITE,
      .fd = fd,
      /* -1, not 0: the write(2) semantic. The kernel reads off unconditionally
       * (rw.c:272); -1 selects the file's own position, degrading to 0 for
       * stream-mode files like the console (rw.c:483-493) — where a literal 0
       * would mean "write at offset zero" on a seekable file. */
      .off = (__u64)-1,
      .addr = (unsigned long)(uintptr_t)buf,
      .len = len,
  });
}

/* The one operation whose arguments do not land in argument-shaped fields:
 * domain in fd, type in off, protocol in len (net.c). Read as a struct
 * description it looks like a mistake; it is what the opcode specifies. */
int rt_socket(int domain, int type, int protocol) {
  return rt_fiber_await_io((struct io_uring_sqe){
      .opcode = IORING_OP_SOCKET,
      .fd = domain,
      .off = (unsigned)type,
      .len = (unsigned)protocol,
  });
}

int rt_bind(int fd, const void *addr, unsigned addr_len) {
  return rt_fiber_await_io((struct io_uring_sqe){
      .opcode = IORING_OP_BIND,
      .fd = fd,
      .addr2 = addr_len,
      .addr = (unsigned long)(uintptr_t)addr,
  });
}

int rt_listen(int fd, int backlog) {
  return rt_fiber_await_io((struct io_uring_sqe){
      .opcode = IORING_OP_LISTEN,
      .fd = fd,
      .len = (unsigned)backlog,
  });
}

/* The absent fields are the specification here, which is this form's real win
 * over assignment: addr and addr2 unset discards the peer address, and ioprio
 * unset makes this one-shot — multishot CQEs do not fit a model where one
 * completion ends one suspension. */
int rt_accept(int fd) {
  return rt_fiber_await_io((struct io_uring_sqe){
      .opcode = IORING_OP_ACCEPT,
      .fd = fd,
  });
}

int rt_recv(int fd, void *buf, unsigned len) {
  return rt_fiber_await_io((struct io_uring_sqe){
      .opcode = IORING_OP_RECV,
      .fd = fd,
      .addr = (unsigned long)(uintptr_t)buf,
      .len = len,
  });
}

int rt_send(int fd, const void *buf, unsigned len) {
  return rt_fiber_await_io((struct io_uring_sqe){
      .opcode = IORING_OP_SEND,
      .fd = fd,
      .addr = (unsigned long)(uintptr_t)buf,
      .len = len,
  });
}

int rt_close(int fd) {
  return rt_fiber_await_io((struct io_uring_sqe){
      .opcode = IORING_OP_CLOSE,
      .fd = fd,
  });
}
