#include "time.h"

unsigned long rt_ticks(void) {
  unsigned long t;
  __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(t) : : "memory");
  return t;
}

unsigned long rt_tick_hz(void) {
  unsigned long hz;
  __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(hz));
  return hz;
}
