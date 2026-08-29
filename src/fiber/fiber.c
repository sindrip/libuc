#include "fiber.h"

#include <stdint.h>

#include <linux/mman.h>

#include "syscall.h"

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
  fiber_context_make(&fiber->context, fiber->stack + stack_length, run_fiber,
                     fiber);
  return true;
}

[[nodiscard]] bool __libuc_fiber_destroy(const struct __libuc_fiber *fiber) {
  return !__libuc_syscall_failed(
      __libuc_sys_munmap(fiber->stack, fiber->stack_length));
}

void __libuc_fiber_run(struct __libuc_fiber *fiber) {
  struct fiber_context home;
  fiber->return_to = &home;
  fiber_switch(&home, &fiber->context);

  /* We are back on the caller's stack. Poison the completed context. */
  fiber->context = (struct fiber_context){0};
  fiber->return_to = nullptr;
}
