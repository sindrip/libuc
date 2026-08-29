#include "fiber/fiber.h"

#include <stddef.h>
#include <stdint.h>

#include "register_probe.h"

static unsigned long argument_witness;
static unsigned long stack_witness;

[[noreturn]] static void observe(void *argument) {
  unsigned char local;
  argument_witness = (unsigned long)(uintptr_t)argument;
  stack_witness = (unsigned long)(uintptr_t)&local;
  dirty_registers_and_finish(argument, fiber_switch);
}

int main(void) {
  struct __libuc_fiber fiber;
  if (!__libuc_fiber_create(&fiber, (size_t)256 * 1024, observe, &fiber)) {
    return 125;
  }

  const unsigned long corrupted = run_probing_registers(&fiber);
  if (corrupted != 0) {
    return (int)(100 + corrupted);
  }

  if (argument_witness != (unsigned long)(uintptr_t)&fiber) {
    return 124;
  }

  const unsigned long base = (unsigned long)(uintptr_t)fiber.stack;
  if (stack_witness < base || stack_witness >= base + fiber.stack_length) {
    return 123;
  }

  if (fiber.return_to != nullptr) {
    return 121;
  }
  if (!__libuc_fiber_destroy(&fiber)) {
    return 120;
  }
  return 0;
}
