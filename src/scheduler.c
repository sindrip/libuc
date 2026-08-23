#include "scheduler.h"

#include <stdatomic.h>
#include <stdint.h>

#include <asm/errno.h>

#include "crash.h"
#include "fiber.h"
#include "syscall.h"

int rt_scheduler_init(struct rt_scheduler *s, unsigned ring_entries) {

  *s = (struct rt_scheduler){};

  auto ret = rt_ring_setup(&s->ring, ring_entries);
  if (sys_failed(ret)) {
    return ret;
  }

  return 0;
}

void rt_scheduler_provide(struct rt_scheduler *s, struct rt_fiber *fibers,
                          size_t count) {
  for (size_t i = 0; i < count; i++) {
    rt_fiber_stack_alloc(&fibers[i]);
    rt_fiber_queue_push(&s->idle, &fibers[i]);
  }
}

int rt_scheduler_spawn(struct rt_scheduler *s, void (*fn)(void *), void *arg) {
  struct rt_fiber *t = rt_fiber_queue_pop(&s->idle);
  if (t == nullptr) {
    return -EAGAIN;
  }

  rt_fiber_start(t, fn, arg);
  s->live_count++;
  rt_fiber_queue_push(&s->ready, t);

  return 0;
}

void rt_scheduler_resume(struct rt_scheduler *s, struct rt_fiber *t) {
  if (t->owner != nullptr) {
    rt_panic("scheduler: resume of a fiber awaiting a completion",
             __builtin_return_address(0));
  }

  t->suspend_to = &s->context;

  t->request.kind = RT_REQUEST_NONE;

  rt_fiber_set_current(t);

  for (;;) {
    rt_switch(&s->context, &t->ctx);

    if (t->request.kind == RT_REQUEST_WAKE) {
      rt_fiber_queue_push(&s->ready, t->request.value.wake);
    } else if (t->request.kind == RT_REQUEST_SPAWN) {
      t->result = rt_scheduler_spawn(s, t->request.value.spawn.fn,
                                     t->request.value.spawn.arg);
    } else {
      break;
    }

    t->request.kind = RT_REQUEST_NONE;
  }

  rt_fiber_set_current(nullptr);
}

static void rt_scheduler_dispatch(struct rt_scheduler *s, struct rt_fiber *t) {
  switch (t->request.kind) {
  case RT_REQUEST_YIELD:
    rt_fiber_queue_push(&s->ready, t);
    break;

  case RT_REQUEST_IO:
    rt_fiber_queue_push(&s->submit, t);
    break;

  case RT_REQUEST_PARK:

    rt_fiber_queue_push(t->request.value.wait_queue, t);
    break;

  case RT_REQUEST_WAKE:

    rt_panic("scheduler: wake reached dispatch", __builtin_return_address(0));

  case RT_REQUEST_SPAWN:

    rt_panic("scheduler: spawn reached dispatch", __builtin_return_address(0));

  case RT_REQUEST_EXIT:
    s->live_count--;
    rt_fiber_queue_push(&s->idle, t);
    break;

  case RT_REQUEST_NONE:

    rt_panic("scheduler: fiber suspended without a request",
             __builtin_return_address(0));
  }
}

void rt_scheduler_run(struct rt_scheduler *s) {
  for (;;) {

    for (size_t n = s->ready.count; n > 0; n--) {
      struct rt_fiber *t = rt_fiber_queue_pop(&s->ready);

      rt_scheduler_resume(s, t);
      rt_scheduler_dispatch(s, t);
    }

    while (s->submit.count > 0) {
      struct io_uring_sqe *sqe = rt_ring_sqe(&s->ring);
      if (sqe == nullptr) {
        break;
      }

      struct rt_fiber *t = rt_fiber_queue_pop(&s->submit);
      *sqe = t->request.value.io;
      sqe->user_data = (unsigned long)(uintptr_t)t;

      t->owner = s;
      s->inflight_count++;
    }

    if (s->live_count == 0) {
      return;
    }

    auto staged = s->ring.cached_sq_tail -
                  atomic_load_explicit(s->ring.sq_tail, memory_order_relaxed);

    bool wait = s->ready.count == 0 && s->submit.count == 0;

    if (wait && s->inflight_count == 0) {
      rt_panic("scheduler: deadlock — every live fiber is parked",
               __builtin_return_address(0));
    }

    if (s->inflight_count > 0) {

      auto ret = rt_ring_submit_and_wait(&s->ring, staged, wait ? 1u : 0u);
      if (sys_failed(ret)) {
        rt_panic("scheduler: enter failed", __builtin_return_address(0));
      }
      if ((unsigned)ret != staged) {
        rt_panic("scheduler: short submit", __builtin_return_address(0));
      }
    }

    struct io_uring_cqe cqe;
    while (rt_ring_reap(&s->ring, &cqe)) {
      struct rt_fiber *t = (struct rt_fiber *)(uintptr_t)cqe.user_data;

      if (t->owner == nullptr) {
        rt_panic("scheduler: cqe for a fiber awaiting nothing",
                 __builtin_return_address(0));
      }

      if (t->owner != s) {
        rt_panic("scheduler: cqe for a fiber owned by another scheduler",
                 __builtin_return_address(0));
      }

      t->result = cqe.res;
      t->owner = nullptr;
      s->inflight_count--;
      rt_fiber_queue_push(&s->ready, t);
    }
  }
}
