#ifndef LIBUC_MEMCMP_ARCH_H
#define LIBUC_MEMCMP_ARCH_H

[[gnu::always_inline]]
static inline bool memcmp_arch_equal(lane64 lhs, lane64 rhs) {
  return __builtin_reduce_max(lhs ^ rhs) == 0;
}

#endif
