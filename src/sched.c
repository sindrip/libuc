/*
 * Scheduler bodies. Contracts and the design doctrine are in sched.h; the
 * ticket's amendments section carries the arguments. Every stub traps until
 * implemented — the same discipline as ring.c: a stub that returned 0 would
 * let the driver run and fail somewhere less honest.
 */

#include "sched.h"

int rt_sched_init(unsigned entries) {
  (void)entries;
  __builtin_trap();
}

int rt_spawn(void (*fn)(void *), void *arg) {
  (void)fn;
  (void)arg;
  __builtin_trap();
}

int rt_nop(void) { __builtin_trap(); }

int rt_write(int fd, const void *buf, unsigned len) {
  (void)fd;
  (void)buf;
  (void)len;
  __builtin_trap();
}

void rt_sched_run(void) { __builtin_trap(); }

const struct rt_task *rt_sched_current(void) { __builtin_trap(); }
