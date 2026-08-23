#include "fiber.h"

#include <stddef.h>
#include <stdint.h>

#include <linux/mman.h>

#include "auxv.h"
#include "crash.h"
#include "syscall.h"

constexpr size_t RT_STACK_SIZE = 64 * 1024;

void rt_fiber_create(struct rt_fiber *f, void (*fn)(void *), void *arg) {
  const size_t guard = rt_page_size();

  if ((RT_STACK_SIZE & (guard - 1)) != 0) {
    rt_panic("fiber: stack size is not a multiple of the page size",
             __builtin_return_address(0));
  }

  auto base = sys_mmap(nullptr, guard + RT_STACK_SIZE, PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (sys_failed(base)) {
    sys_exit_group((int)-base);
  }

  *f = (struct rt_fiber){};

  f->stack_base = (void *)(uintptr_t)base;
  f->stack_len = guard + RT_STACK_SIZE;
  f->fn = fn;
  f->arg = arg;

  auto ret = sys_mprotect((char *)f->stack_base + guard, RT_STACK_SIZE,
                          PROT_READ | PROT_WRITE);
  if (sys_failed(ret)) {
    sys_exit_group(-ret);
  }

  rt_ctx_init(&f->ctx, (char *)f->stack_base + f->stack_len, fn, arg);
}

void rt_fiber_queue_push(struct rt_fiber_queue *q, struct rt_fiber *t) {
  t->ready_next = nullptr;
  if (q->tail == nullptr) {
    q->head = t;
  } else {
    q->tail->ready_next = t;
  }
  q->tail = t;
  q->count++;
}

struct rt_fiber *rt_fiber_queue_pop(struct rt_fiber_queue *q) {
  struct rt_fiber *t = q->head;

  if (t == nullptr) {
    return nullptr;
  }

  q->head = t->ready_next;
  if (q->head == nullptr) {
    q->tail = nullptr;
  }
  t->ready_next = nullptr;
  q->count--;

  return t;
}

static struct rt_fiber *current;

struct rt_fiber *rt_fiber_current(void) { return current; }
void rt_fiber_set_current(struct rt_fiber *f) { current = f; }

static void rt_fiber_suspend(struct rt_fiber *self) {
  rt_switch(&self->ctx, self->suspend_to);
}

void rt_fiber_yield(void) {
  struct rt_fiber *self = rt_fiber_current();

  self->request = (struct rt_fiber_request){.kind = RT_REQUEST_YIELD};
  rt_fiber_suspend(self);
}

void rt_fiber_park(struct rt_fiber_queue *q) {
  struct rt_fiber *self = rt_fiber_current();

  self->request = (struct rt_fiber_request){
      .kind = RT_REQUEST_PARK,
      .value = {.wait_queue = q},
  };
  rt_fiber_suspend(self);
}

void rt_fiber_wake(struct rt_fiber *f) {
  struct rt_fiber *self = rt_fiber_current();

  self->request = (struct rt_fiber_request){
      .kind = RT_REQUEST_WAKE,
      .value = {.wake = f},
  };
  rt_fiber_suspend(self);
}

int rt_fiber_await_io(struct io_uring_sqe sqe) {
  struct rt_fiber *self = rt_fiber_current();

  self->request = (struct rt_fiber_request){
      .kind = RT_REQUEST_IO,
      .value = {.io = sqe},
  };

  rt_fiber_suspend(self);

  return self->result;
}

[[noreturn]] void rt_fiber_exit(void) {
  struct rt_fiber *self = rt_fiber_current();

  self->request = (struct rt_fiber_request){.kind = RT_REQUEST_EXIT};
  rt_fiber_suspend(self);

  __builtin_trap();
}
