#include "fiber.h"

#include <stdint.h>

#include <linux/mman.h>

#include "syscall.h"
#include "thread_local_arch.h"

[[noreturn]] static void run_fiber(void *opaque) {
  struct __libuc_fiber *fiber = opaque;
  fiber->entry(fiber->argument);
  fiber_switch(&fiber->context, fiber->return_to);
  __builtin_trap();
}

[[nodiscard]] bool __libuc_fiber_create(struct __libuc_fiber *fiber,
                                        size_t stack_length,
                                        void (*entry)(void *), void *argument) {
  const long address =
      __libuc_sys_mmap(nullptr, stack_length, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (__libuc_syscall_failed(address)) {
    return false;
  }

  *fiber = (struct __libuc_fiber){
      .entry = entry,
      .argument = argument,
      .stack = (unsigned char *)(uintptr_t)address,
      .stack_length = stack_length,
  };

  if (!__libuc_thread_local_block_create(&fiber->thread_local_block)) {
    (void)__libuc_sys_munmap(fiber->stack, stack_length);
    return false;
  }

  struct __libuc_thread_local_tcb *tcb =
      fiber->thread_local_block.thread_pointer;
  tcb->fiber = fiber;

  fiber_context_make(&fiber->context, fiber->stack + stack_length, run_fiber,
                     fiber);
  return true;
}

[[nodiscard]] bool __libuc_fiber_destroy(const struct __libuc_fiber *fiber) {
  const bool block_destroyed =
      __libuc_thread_local_block_destroy(&fiber->thread_local_block);
  const bool stack_released = !__libuc_syscall_failed(
      __libuc_sys_munmap(fiber->stack, fiber->stack_length));

  return block_destroyed && stack_released;
}

void __libuc_fiber_run(struct __libuc_fiber *fiber) {
  struct fiber_context home;
  fiber->return_to = &home;

  void *caller_thread_pointer = thread_local_read();
  thread_local_install(fiber->thread_local_block.thread_pointer);
  fiber_switch(&home, &fiber->context);
  thread_local_install(caller_thread_pointer);

  /* We are back on the caller's stack. Poison the completed context. */
  fiber->context = (struct fiber_context){0};
  fiber->return_to = nullptr;
}

[[nodiscard]] struct __libuc_fiber *__libuc_fiber_current(void) {
  void *thread_pointer = thread_local_read();
  if (thread_pointer == nullptr) {
    return nullptr;
  }

  return ((struct __libuc_thread_local_tcb *)thread_pointer)->fiber;
}
