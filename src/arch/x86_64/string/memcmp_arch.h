#ifndef LIBUC_MEMCMP_ARCH_H
#define LIBUC_MEMCMP_ARCH_H

/* The fold x86 tests cheapest: OR over word lanes. */
typedef unsigned _BitInt(64) memcmp_acc;

[[gnu::always_inline]]
static inline memcmp_acc memcmp_accumulate(memcmp_acc acc, memcmp_acc x) {
  return acc | x;
}

#endif
