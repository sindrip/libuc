#include "fiber/fiber.h"

#include <stddef.h>
#include <stdint.h>

static unsigned long argument_witness;
static unsigned long stack_witness;

static void observe(void *argument) {
  unsigned char local;
  argument_witness = (unsigned long)(uintptr_t)argument;
  stack_witness = (unsigned long)(uintptr_t)&local;
}

int main(void) {
  unsigned long held[10];
  for (unsigned long index = 0; index < 10; index++) {
    held[index] = 0x1000 + index * 7;
  }

  struct __libuc_fiber fiber;
  if (!__libuc_fiber_create(&fiber, (size_t)256 * 1024, observe,
                            (void *)(uintptr_t)0x5eed)) {
    return 125;
  }
  __libuc_fiber_run(&fiber);

  if (argument_witness != 0x5eed) {
    return 124;
  }

  const unsigned long base = (unsigned long)(uintptr_t)fiber.stack;
  if (stack_witness < base || stack_witness >= base + fiber.stack_length) {
    return 123;
  }

  unsigned long sum = 0;
  for (unsigned long index = 0; index < 10; index++) {
    sum += held[index];
  }
  if (sum != 10 * 0x1000 + 7 * 45) {
    return 122;
  }

  if (fiber.return_to != nullptr) {
    return 121;
  }
  if (!__libuc_fiber_destroy(&fiber)) {
    return 120;
  }
  return 0;
}
