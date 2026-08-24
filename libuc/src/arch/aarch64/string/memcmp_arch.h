#ifndef LIBUC_MEMCMP_ARCH_H
#define LIBUC_MEMCMP_ARCH_H

#include <stddef.h>

typedef unsigned char memcmp_block [[gnu::vector_size(64)]];

constexpr size_t memcmp_arch_block_size = sizeof(memcmp_block);

[[gnu::always_inline]]
static inline bool memcmp_arch_equal(const unsigned char *lhs,
                                     const unsigned char *rhs) {
  memcmp_block a;
  memcmp_block b;
  __builtin_memcpy(&a, lhs, sizeof(a));
  __builtin_memcpy(&b, rhs, sizeof(b));
  return __builtin_reduce_max(a ^ b) == 0;
}

#endif
