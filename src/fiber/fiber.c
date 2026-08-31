#include "fiber.h"

#include <stdint.h>

#include <linux/mman.h>

#include "syscall.h"
#include "thread_local_arch.h"

[[noreturn]] static void run_fiber(void *opaque) {
  struct __libuc_fiber *fiber = opaque;
  fiber->status = fiber->entry(fiber->argument);

  fiber->request = __LIBUC_FIBER_REQUEST_EXIT;
  fiber_switch(&fiber->context, fiber->return_to);
  __builtin_trap();
}

[[nodiscard]] struct __libuc_fiber *__libuc_fiber_spawn(size_t stack_length,
                                                        int (*entry)(void *),
                                                        void *argument) {
  if (stack_length <= sizeof(struct __libuc_fiber)) {
    return nullptr;
  }

  const long address =
      __libuc_sys_mmap(nullptr, stack_length, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (__libuc_syscall_failed(address)) {
    return nullptr;
  }
  unsigned char *stack = (unsigned char *)(uintptr_t)address;

  /* The record takes the top of the mapping, so the stack grows down away
   * from it and an overflow runs off the low end instead. */
  const uintptr_t top =
      (uintptr_t)(stack + stack_length) - sizeof(struct __libuc_fiber);
  struct __libuc_fiber *fiber = (struct __libuc_fiber *)(void *)(
      top & ~(uintptr_t)(alignof(struct __libuc_fiber) - 1));

  *fiber = (struct __libuc_fiber){
      .entry = entry,
      .argument = argument,
      .stack = stack,
      .stack_length = stack_length,
  };

  if (!__libuc_thread_local_block_create(&fiber->thread_local_block)) {
    (void)__libuc_sys_munmap(stack, stack_length);
    return nullptr;
  }

  struct __libuc_thread_local_tcb *tcb =
      fiber->thread_local_block.thread_pointer;
  tcb->fiber = fiber;

  fiber_context_make(&fiber->context, (unsigned char *)fiber, run_fiber, fiber);
  return fiber;
}

[[nodiscard]] bool __libuc_fiber_destroy(const struct __libuc_fiber *fiber) {
  const bool block_destroyed =
      __libuc_thread_local_block_destroy(&fiber->thread_local_block);

  /* A spawned record lives in the mapping being released, so read its
   * geometry out before the unmap. */
  unsigned char *const stack = fiber->stack;
  const size_t stack_length = fiber->stack_length;
  const bool stack_released =
      !__libuc_syscall_failed(__libuc_sys_munmap(stack, stack_length));

  return block_destroyed && stack_released;
}

[[nodiscard]] enum __libuc_fiber_request
__libuc_fiber_resume(struct __libuc_fiber *fiber) {
  struct fiber_context here;
  fiber->return_to = &here;
  fiber->request = __LIBUC_FIBER_REQUEST_NONE;

  void *caller_thread_pointer = thread_local_read();
  thread_local_install(fiber->thread_local_block.thread_pointer);
  fiber_switch(&here, &fiber->context);
  thread_local_install(caller_thread_pointer);

  if (fiber->request == __LIBUC_FIBER_REQUEST_EXIT) {
    fiber->context = (struct fiber_context){0};
  }
  fiber->return_to = nullptr;

  return fiber->request;
}

void __libuc_fiber_yield(void) {
  struct __libuc_fiber *fiber = __libuc_fiber_current();
  fiber->request = __LIBUC_FIBER_REQUEST_YIELD;

  fiber_switch(&fiber->context, fiber->return_to);
}

[[nodiscard]] long __libuc_fiber_await(const struct io_uring_sqe *sqe) {
  struct __libuc_fiber *fiber = __libuc_fiber_current();
  fiber->request_argument = sqe;
  fiber->request = __LIBUC_FIBER_REQUEST_AWAIT;

  fiber_switch(&fiber->context, fiber->return_to);

  return fiber->request_result;
}

[[nodiscard]] struct __libuc_fiber *
__libuc_fiber_start(size_t stack_length, int (*entry)(void *),
                    void *argument) {
  struct __libuc_fiber *fiber = __libuc_fiber_current();
  struct __libuc_fiber_spawn_request request = {
      .stack_length = stack_length,
      .entry = entry,
      .argument = argument,
  };

  fiber->request_argument = &request;
  fiber->request = __LIBUC_FIBER_REQUEST_SPAWN;
  fiber_switch(&fiber->context, fiber->return_to);

  return (struct __libuc_fiber *)(uintptr_t)fiber->request_result;
}

[[nodiscard]] struct __libuc_fiber *__libuc_fiber_current(void) {
  void *thread_pointer = thread_local_read();
  if (thread_pointer == nullptr) {
    return nullptr;
  }

  return ((struct __libuc_thread_local_tcb *)thread_pointer)->fiber;
}
