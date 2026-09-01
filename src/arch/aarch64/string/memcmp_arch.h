#ifndef LIBUC_MEMCMP_ARCH_H
#define LIBUC_MEMCMP_ARCH_H

/* The fold NEON tests cheapest: max over byte lanes. */
typedef unsigned _BitInt(8) memcmp_acc;

[[gnu::always_inline]]
static inline memcmp_acc memcmp_accumulate(memcmp_acc acc, memcmp_acc x) {
  return x > acc ? x : acc;
}

#endif
