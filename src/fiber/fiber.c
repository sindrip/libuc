#include "fiber.h"

#include <stdint.h>

#include <linux/mman.h>

#include "syscall.h"
#include "thread_local_arch.h"

/* By pointer, not thread_local_read(): an entry may return with a
 * foreign thread-local block installed. */
[[noreturn]] static void exit_fiber(struct __libuc_fiber *fiber, int status) {
  fiber->status = status;

  /* The frame is finished but its stack is mapped until the fiber is
   * destroyed, and the scheduler reads this before that can happen. */
  struct __libuc_fiber_request request = {
      .kind = __LIBUC_FIBER_REQUEST_EXIT,
  };

  (void)fiber_switch(&fiber->context, fiber->return_to,
                     (unsigned long)(uintptr_t)&request);
  __builtin_trap();
}

[[noreturn]] static void run_fiber(void *opaque) {
  struct __libuc_fiber *fiber = opaque;
  exit_fiber(fiber, fiber->entry(fiber->argument));
}

[[noreturn]] void __libuc_fiber_exit(int status) {
  exit_fiber(__libuc_fiber_current(), status);
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

[[nodiscard]] struct __libuc_fiber_request *
__libuc_fiber_resume(struct __libuc_fiber *fiber) {
  struct fiber_context here;
  fiber->return_to = &here;

  void *caller_thread_pointer = thread_local_read();
  thread_local_install(fiber->thread_local_block.thread_pointer);
  struct __libuc_fiber_request *request =
      (struct __libuc_fiber_request *)(uintptr_t)fiber_switch(
          &here, &fiber->context, 0);
  thread_local_install(caller_thread_pointer);

  if (request == nullptr) {
    __builtin_trap();
  }
  if (request->kind == __LIBUC_FIBER_REQUEST_EXIT) {
    fiber->context = (struct fiber_context){0};
    /* A detach mark outlives the exit; the scheduler reclaims by it. */
    if (fiber->life == __LIBUC_FIBER_LIVE) {
      fiber->life = __LIBUC_FIBER_EXITED;
    }
  }
  fiber->return_to = nullptr;
  request->fiber = fiber;

  return request;
}

void __libuc_fiber_yield(void) {
  struct __libuc_fiber *fiber = __libuc_fiber_current();
  struct __libuc_fiber_request request = {
      .kind = __LIBUC_FIBER_REQUEST_YIELD,
  };

  (void)fiber_switch(&fiber->context, fiber->return_to,
                     (unsigned long)(uintptr_t)&request);
}

[[nodiscard]] long __libuc_fiber_await(const struct io_uring_sqe *sqe) {
  struct __libuc_fiber *fiber = __libuc_fiber_current();
  struct __libuc_fiber_request request = {
      .kind = __LIBUC_FIBER_REQUEST_AWAIT,
      .argument = sqe,
  };

  (void)fiber_switch(&fiber->context, fiber->return_to,
                     (unsigned long)(uintptr_t)&request);

  return request.result;
}

[[nodiscard]] long __libuc_fiber_join(struct __libuc_fiber *target) {
  struct __libuc_fiber *fiber = __libuc_fiber_current();
  struct __libuc_fiber_request request = {
      .kind = __LIBUC_FIBER_REQUEST_JOIN,
      .target = target,
  };

  (void)fiber_switch(&fiber->context, fiber->return_to,
                     (unsigned long)(uintptr_t)&request);

  return request.result;
}

[[noreturn]] void __libuc_fiber_process_exit(struct __libuc_fiber *fiber,
                                             int status) {
  struct __libuc_fiber_request request = {
      .kind = __LIBUC_FIBER_REQUEST_PROCESS_EXIT,
      .status = status,
  };

  (void)fiber_switch(&fiber->context, fiber->return_to,
                     (unsigned long)(uintptr_t)&request);
  __builtin_trap();
}

void __libuc_fiber_detach(struct __libuc_fiber *target) {
  struct __libuc_fiber *fiber = __libuc_fiber_current();
  struct __libuc_fiber_request request = {
      .kind = __LIBUC_FIBER_REQUEST_DETACH,
      .target = target,
  };

  (void)fiber_switch(&fiber->context, fiber->return_to,
                     (unsigned long)(uintptr_t)&request);
}

[[nodiscard]] struct __libuc_fiber *
__libuc_fiber_start(size_t stack_length, int (*entry)(void *),
                    void *argument) {
  struct __libuc_fiber *fiber = __libuc_fiber_current();
  const struct __libuc_fiber_spawn_request spawn = {
      .stack_length = stack_length,
      .entry = entry,
      .argument = argument,
  };
  struct __libuc_fiber_request request = {
      .kind = __LIBUC_FIBER_REQUEST_SPAWN,
      .argument = &spawn,
  };

  (void)fiber_switch(&fiber->context, fiber->return_to,
                     (unsigned long)(uintptr_t)&request);

  return (struct __libuc_fiber *)(uintptr_t)request.result;
}

[[nodiscard]] struct __libuc_fiber *__libuc_fiber_current(void) {
  void *thread_pointer = thread_local_read();
  if (thread_pointer == nullptr) {
    return nullptr;
  }

  return ((struct __libuc_thread_local_tcb *)thread_pointer)->fiber;
}
