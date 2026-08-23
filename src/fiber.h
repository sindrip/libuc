#ifndef RT_FIBER_H
#define RT_FIBER_H

#include <stddef.h>

#include <linux/io_uring.h>

#include "context.h"

struct rt_fiber_queue {
  struct rt_fiber *head;
  struct rt_fiber *tail;
  size_t count;
};

struct rt_scheduler;

enum rt_fiber_request_kind : unsigned {
  RT_REQUEST_NONE = 0,
  RT_REQUEST_YIELD = 1,
  RT_REQUEST_IO = 2,
  RT_REQUEST_EXIT = 3,
  RT_REQUEST_PARK = 4,
  RT_REQUEST_WAKE = 5
};

struct rt_fiber_request {
  enum rt_fiber_request_kind kind;

  union {

    struct io_uring_sqe io;

    struct rt_fiber_queue *wait_queue;

    struct rt_fiber *wake;
  } value;
};

struct rt_fiber {
  struct rt_ctx ctx;
  void *stack_base;
  size_t stack_len;
  int result;

  void (*fn)(void *);
  void *arg;

  struct rt_scheduler *owner;

  struct rt_fiber *ready_next;

  struct rt_fiber_request request;

  struct rt_ctx *suspend_to;
};

void rt_fiber_create(struct rt_fiber *f, void (*fn)(void *), void *arg);

[[nodiscard]] struct rt_fiber *rt_fiber_current(void);
void rt_fiber_set_current(struct rt_fiber *f);

void rt_fiber_yield(void);
[[nodiscard]] int rt_fiber_await_io(struct io_uring_sqe sqe);

void rt_fiber_park(struct rt_fiber_queue *q);

void rt_fiber_wake(struct rt_fiber *f);

void rt_fiber_queue_push(struct rt_fiber_queue *q, struct rt_fiber *f);
[[nodiscard]] struct rt_fiber *rt_fiber_queue_pop(struct rt_fiber_queue *q);

[[noreturn]] void rt_fiber_exit(void);

#endif
