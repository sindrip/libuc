#ifndef LIBUC_SRC_SYSCALL_H
#define LIBUC_SRC_SYSCALL_H

#include <stddef.h>
#include <stdint.h>

#include <asm/unistd.h>

#include "syscall_arch.h"

[[nodiscard]] static inline bool __libuc_syscall_failed(long result) {
  return result < 0 && result >= -4095;
}

[[nodiscard]] static inline long __libuc_sys_mmap(void *address, size_t length,
                                                  int protection, int flags,
                                                  int descriptor,
                                                  unsigned long offset) {
  return __libuc_syscall6(__NR_mmap, (long)(uintptr_t)address, (long)length,
                          protection, flags, descriptor, (long)offset);
}

[[nodiscard]] static inline long __libuc_sys_munmap(void *address,
                                                    size_t length) {
  return __libuc_syscall2(__NR_munmap, (long)(uintptr_t)address, (long)length);
}

#endif
