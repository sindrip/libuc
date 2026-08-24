#ifndef LIBUC_MEMCMP_ARCH_H
#define LIBUC_MEMCMP_ARCH_H

#include <stddef.h>

typedef unsigned char memcmp_lane [[gnu::vector_size(64)]];

constexpr size_t memcmp_arch_width = sizeof(memcmp_lane);

[[gnu::always_inline]]
static inline bool memcmp_arch_equal(const unsigned char *a,
                                     const unsigned char *b) {
  memcmp_lane av;
  memcmp_lane bv;
  __builtin_memcpy(&av, a, sizeof(av));
  __builtin_memcpy(&bv, b, sizeof(bv));
  return __builtin_reduce_max(av ^ bv) == 0;
}

#endif
