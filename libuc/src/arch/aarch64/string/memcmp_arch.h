#ifndef LIBUC_MEMCMP_ARCH_H
#define LIBUC_MEMCMP_ARCH_H

#include <stddef.h>

typedef unsigned char memcmp_block [[gnu::vector_size(64)]];

constexpr size_t memcmp_arch_block_size = sizeof(memcmp_block);

[[gnu::always_inline]]
static inline bool memcmp_arch_equal(const unsigned char *a,
                                     const unsigned char *b) {
  memcmp_block av;
  memcmp_block bv;
  __builtin_memcpy(&av, a, sizeof(av));
  __builtin_memcpy(&bv, b, sizeof(bv));
  return __builtin_reduce_max(av ^ bv) == 0;
}

#endif
