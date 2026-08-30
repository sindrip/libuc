#include "fiber/fiber.h"

#include <stddef.h>
#include <stdint.h>

#include "register_probe.h"
#include "thread_local/thread_local.h"

static unsigned long argument_witness;
static unsigned long stack_witness;

static void observe(void *argument) {
  unsigned char local;
  argument_witness = (unsigned long)(uintptr_t)argument;
  stack_witness = (unsigned long)(uintptr_t)&local;

  dirty_registers_and_yield();
}

int main(void) {
  /* 126: running a fiber installs its thread pointer, which this
   * environment's user mode cannot do. */
  if (!__libuc_thread_local_install_available()) {
    return 126;
  }

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
  if (__libuc_fiber_resume(&fiber) != __LIBUC_FIBER_REQUEST_EXIT) {
    return 119;
  }
  if (!__libuc_fiber_destroy(&fiber)) {
    return 120;
  }
  return 0;
}
