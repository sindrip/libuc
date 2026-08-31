#ifndef LIBUC_TEST_ARCH_AARCH64_BENCH_CLOCK_ARCH_H
#define LIBUC_TEST_ARCH_AARCH64_BENCH_CLOCK_ARCH_H

#include <stdint.h>

/* The virtual counter behind the vDSO clock; EL0-readable, isb so the
 * read cannot hoist above the work it bounds. */
static inline uint64_t bench_ticks(void) {
  uint64_t value;
  __asm__ volatile("isb\n\tmrs %0, cntvct_el0" : "=r"(value) : : "memory");
  return value;
}

static inline uint64_t bench_ticks_per_second(void) {
  uint64_t value;
  __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
  return value;
}

#endif
