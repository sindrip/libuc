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

      .off = (__u64)-1,
      .addr = (unsigned long)(uintptr_t)buf,
      .len = len,
  });
}

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
