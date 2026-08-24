#ifndef LIBUC_MEMCMP_ARCH_H
#define LIBUC_MEMCMP_ARCH_H

#include <stddef.h>

// The window is the architecture's: its width and the equality test native to
// it travel together, so generic code never names a vector type. pcmpeqb into
// pmovmskb is the across-vector test here; the max reduction aarch64 uses
// lowers to a pmaxub shift cascade that costs ten more instructions per
// window, which is why the test lives per architecture.
//
// Four registers per window, so the width follows the widest register the
// build was told it has: 16 bytes at the SSE2 baseline, 32 under AVX2. A
// window wider than the registers backing it only unrolls the same work.
#ifdef __AVX2__
typedef unsigned char memcmp_lane [[gnu::vector_size(128)]];
#else
typedef unsigned char memcmp_lane [[gnu::vector_size(64)]];
#endif

constexpr size_t memcmp_arch_width = sizeof(memcmp_lane);

[[gnu::always_inline]]
static inline bool memcmp_arch_equal(const unsigned char *a,
                                     const unsigned char *b) {
  memcmp_lane av;
  memcmp_lane bv;
  __builtin_memcpy(&av, a, sizeof(av));
  __builtin_memcpy(&bv, b, sizeof(bv));
  return __builtin_reduce_and(av == bv) != 0;
}

#endif
