#ifndef LIBUC_MEMCMP_ARCH_H
#define LIBUC_MEMCMP_ARCH_H

#include <stddef.h>

#ifdef __AVX2__
typedef unsigned char memcmp_block [[gnu::vector_size(128)]];
#else
typedef unsigned char memcmp_block [[gnu::vector_size(64)]];
#endif

constexpr size_t memcmp_arch_block_size = sizeof(memcmp_block);

[[gnu::always_inline]]
static inline bool memcmp_arch_equal(const unsigned char *l,
                                     const unsigned char *r) {
  memcmp_block a;
  memcmp_block b;
  __builtin_memcpy(&a, l, sizeof(a));
  __builtin_memcpy(&b, r, sizeof(b));
  return __builtin_reduce_and(a == b) != 0;
}

#endif
