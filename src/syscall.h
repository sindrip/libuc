#ifndef RT_SYSCALL_H
#define RT_SYSCALL_H

#include "arch/aarch64/syscall_arch.h"

#include <stddef.h>
#include <stdint.h>

#include <asm/unistd.h>
#include <asm/unistd_64.h>

#include <asm/signal.h>

[[nodiscard]] static inline bool sys_failed(long r) {
  return r < 0 && r >= -4095;
}

static inline long raw_write(int fd, const void *buf, size_t len) {
  return syscall3(__NR_write, fd, (long)(uintptr_t)buf, (long)len);
}

[[noreturn]] static inline void sys_exit_group(int status) {
  syscall1(__NR_exit_group, status);

  unreachable();
}

[[nodiscard]] static inline long sys_mmap(void *addr, size_t len, int prot,
                                          int flags, int fd,
                                          unsigned long off) {
  return syscall6(__NR_mmap, (long)(uintptr_t)addr, (long)len, prot, flags, fd,
                  (long)off);
}

[[nodiscard]] static inline int sys_mprotect(void *addr, size_t len, int prot) {
  return (int)syscall3(__NR_mprotect, (long)(uintptr_t)addr, (long)len, prot);
}

struct io_uring_params;

[[nodiscard]] static inline int sys_io_uring_setup(unsigned entries,
                                                   struct io_uring_params *p) {
  return (int)syscall2(__NR_io_uring_setup, entries, (long)(uintptr_t)p);
}

[[nodiscard]] static inline int
sys_io_uring_enter(int fd, unsigned to_submit, unsigned min_complete,
                   unsigned flags, const void *arg, size_t argsz) {
  return (int)syscall6(__NR_io_uring_enter, fd, to_submit, min_complete, flags,
                       (long)(uintptr_t)arg, (long)argsz);
}

[[nodiscard]] static inline int
sys_io_uring_register(int fd, unsigned opcode, void *arg, unsigned nr_args) {
  return (int)syscall4(__NR_io_uring_register, fd, opcode, (long)(uintptr_t)arg,
                       nr_args);
}

[[nodiscard]] static inline int sys_rt_sigaction(int signum,
                                                 const struct sigaction *act,
                                                 struct sigaction *oldact) {
  return (int)syscall4(__NR_rt_sigaction, signum, (long)(uintptr_t)act,
                       (long)(uintptr_t)oldact, (long)sizeof(sigset_t));
}

[[nodiscard]] static inline int sys_sigaltstack(const struct sigaltstack *ss,
                                                struct sigaltstack *old_ss) {
  return (int)syscall2(__NR_sigaltstack, (long)(uintptr_t)ss,
                       (long)(uintptr_t)old_ss);
}

#endif
