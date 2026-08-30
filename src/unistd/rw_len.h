#ifndef LIBUC_SRC_UNISTD_RW_LEN_H
#define LIBUC_SRC_UNISTD_RW_LEN_H

#include <stddef.h>
#include <stdint.h>

/* Prevents modulo-2^32 narrowing; the kernel's MAX_RW_COUNT cap keeps
 * every outcome identical. */
static inline uint32_t __libuc_rw_len(size_t count) {
  constexpr size_t limit = INT32_MAX;

  return (uint32_t)(count > limit ? limit : count);
}

#endif
