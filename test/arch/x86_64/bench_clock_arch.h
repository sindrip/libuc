#ifndef LIBUC_TEST_ARCH_X86_64_BENCH_CLOCK_ARCH_H
#define LIBUC_TEST_ARCH_X86_64_BENCH_CLOCK_ARCH_H

#include <stdint.h>

static inline uint64_t bench_ticks(void) {
  uint32_t low;
  uint32_t high;
  __asm__ volatile("lfence\n\trdtsc" : "=a"(low), "=d"(high) : : "memory");
  return (uint64_t)high << 32 | low;
}

/* The TSC frequency is not architecturally readable; zero means report
 * ticks only. */
static inline uint64_t bench_ticks_per_second(void) { return 0; }

#endif
