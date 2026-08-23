#ifndef RT_SCHEDULER_H
#define RT_SCHEDULER_H

#include <stddef.h>

#include "ring.h"
#include "fiber.h"

struct rt_scheduler {

  struct rt_ctx context;

  struct rt_ring ring;

  struct rt_fiber_queue ready;
  struct rt_fiber_queue submit;
  struct rt_fiber_queue idle;

  size_t live_count;
  size_t inflight_count;

};

[[nodiscard]] int rt_scheduler_init(struct rt_scheduler *s,
                                    unsigned ring_entries);

void rt_scheduler_provide(struct rt_scheduler *s, struct rt_fiber *fibers,
                          size_t count);

[[nodiscard]] int rt_scheduler_spawn(struct rt_scheduler *s,
                                     void (*fn)(void *), void *arg);

void rt_scheduler_run(struct rt_scheduler *s);

void rt_scheduler_resume(struct rt_scheduler *s, struct rt_fiber *t);

#endif
