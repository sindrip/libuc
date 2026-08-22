/*
 * Scheduler bodies. Contracts are in sched.h; the ticket carries the spec.
 * Every stub traps until implemented — the same discipline as ring.c: a
 * stub that returned 0 would let the driver run and fail somewhere less
 * honest.
 */

#include "sched.h"

int rt_sched_init(unsigned entries) {
  (void)entries;
  __builtin_trap();
}

int rt_nop(void) { __builtin_trap(); }

int rt_write(int fd, const void *buf, unsigned len) {
  (void)fd;
  (void)buf;
  (void)len;
  __builtin_trap();
}

void rt_sched_run(struct rt_task **tasks, int ntasks) {
  (void)tasks;
  (void)ntasks;
  __builtin_trap();
}
