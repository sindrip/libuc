#include "fiber.h"

#include <stddef.h>
#include <stdint.h>

#include <linux/mman.h>

#include "auxv.h"
#include "crash.h"
#include "syscall.h"

constexpr size_t RT_STACK_SIZE = 64 * 1024;

static_assert((RT_STACK_SIZE & (RT_STACK_SIZE - 1)) == 0);

constexpr unsigned long RT_STACK_MAGIC = 0xF1BE57ACC0DE0001UL;

struct rt_stack_head {
  unsigned long magic;
  struct rt_fiber *fiber;
};

static struct rt_stack_head *rt_stack_head_of(uintptr_t block) {
  return (struct rt_stack_head *)(block + RT_STACK_SIZE) - 1;
}

void rt_fiber_stack_alloc(struct rt_fiber *f) {
  const size_t guard = rt_page_size();

  if ((RT_STACK_SIZE & (guard - 1)) != 0) {
    rt_panic("fiber: stack size is not a multiple of the page size",
             __builtin_return_address(0));
  }

  auto raw = sys_mmap(nullptr, 2 * RT_STACK_SIZE, PROT_NONE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (sys_failed(raw)) {
    sys_exit_group((int)-raw);
  }

  const uintptr_t base = (uintptr_t)raw;
  const uintptr_t block =
      (base + RT_STACK_SIZE) & ~(uintptr_t)(RT_STACK_SIZE - 1);

  const size_t front = (block - guard) - base;
  if (front != 0) {
    auto ret = sys_munmap((void *)base, front);
    if (sys_failed(ret)) {
      sys_exit_group(-ret);
    }
  }

  const size_t back = (base + 2 * RT_STACK_SIZE) - (block + RT_STACK_SIZE);
  if (back != 0) {
    auto ret = sys_munmap((void *)(block + RT_STACK_SIZE), back);
    if (sys_failed(ret)) {
      sys_exit_group(-ret);
    }
  }

  *f = (struct rt_fiber){};

  f->stack_base = (void *)block;
  f->stack_len = RT_STACK_SIZE;

  auto ret = sys_mprotect(f->stack_base, RT_STACK_SIZE, PROT_READ | PROT_WRITE);
  if (sys_failed(ret)) {
    sys_exit_group(-ret);
  }

  *rt_stack_head_of(block) = (struct rt_stack_head){
      .magic = RT_STACK_MAGIC,
      .fiber = f,
  };
}

void rt_fiber_start(struct rt_fiber *f, void (*fn)(void *), void *arg) {
  if (f->stack_base == nullptr) {
    rt_panic("fiber: start without a stack", __builtin_return_address(0));
  }

  if (f->owner != nullptr) {
    rt_panic("fiber: start of a fiber awaiting a completion",
             __builtin_return_address(0));
  }

  f->result = 0;
  f->ready_next = nullptr;
  f->request = (struct rt_fiber_request){};
  f->suspend_to = nullptr;

  rt_ctx_init(&f->ctx, rt_stack_head_of((uintptr_t)f->stack_base), fn, arg);
}

void rt_fiber_create(struct rt_fiber *f, void (*fn)(void *), void *arg) {
  rt_fiber_stack_alloc(f);
  rt_fiber_start(f, fn, arg);
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

struct rt_fiber *rt_fiber_current(void) {
  void *frame = __builtin_frame_address(0);
  struct rt_stack_head *head =
      rt_stack_head_of((uintptr_t)frame & ~(uintptr_t)(RT_STACK_SIZE - 1));

  if (head->magic != RT_STACK_MAGIC) {
    rt_panic("fiber: not running on a fiber stack",
             __builtin_return_address(0));
  }

  return head->fiber;
}

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

bool rt_fiber_wake_one(struct rt_fiber_queue *q) {
  struct rt_fiber *self = rt_fiber_current();

  self->request = (struct rt_fiber_request){
      .kind = RT_REQUEST_WAKE,
      .value = {.wait_queue = q},
  };
  rt_fiber_suspend(self);

  return self->result != 0;
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

int rt_fiber_spawn(void (*fn)(void *), void *arg) {
  struct rt_fiber *self = rt_fiber_current();

  self->request = (struct rt_fiber_request){
      .kind = RT_REQUEST_SPAWN,
      .value = {.spawn = {.fn = fn, .arg = arg}},
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
