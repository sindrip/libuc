#ifndef LIBUC_SRC_SYSCALL_H
#define LIBUC_SRC_SYSCALL_H

#include <stddef.h>
#include <stdint.h>

#include <asm/unistd.h>

#include "syscall_arch.h"

/* Internal only. Generic code calls the typed __libuc_sys_* wrappers below;
 * __libuc_syscall6 is the architecture seam, not an operation interface.
 * Success values pass through unchanged and failures are returned as -errno.
 * Public libc interfaces apply their own error policy. */
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

#endif
