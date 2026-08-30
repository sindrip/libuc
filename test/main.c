#include <stddef.h>
#include <stdint.h>

#include <string.h>
#include <sys/auxv.h>

#include "auxv.h"
#include "thread_local/thread_local.h"

/* Initializers must run before main, in priority order: numbered slots
 * first (101 before 202; 1..100 are reserved by the toolchain), then the
 * unprioritized one after every numbered slot. Each stage refuses to
 * advance unless the previous one already happened. */
static volatile int stage;
static uintptr_t constructor_stack_witness;

static _Thread_local volatile int tls_initialized = 42;
static _Thread_local volatile int tls_zeroed;

/* -Wglobal-constructors exists to flag initialization hiding before main;
 * proving that initialization runs is this file's entire purpose. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wglobal-constructors"

[[gnu::constructor(101)]] static void init_first(void) {
  uintptr_t page_size;
  uintptr_t absent_value;
  int initial_value = 0;
  unsigned char stack_probe;

  constructor_stack_witness = (uintptr_t)&stack_probe;

  const struct __libuc_thread_local_layout *thread_local_layout =
      __libuc_thread_local_layout();
  const bool thread_local_layout_stable =
      __libuc_thread_local_layout() == thread_local_layout;
  if (thread_local_layout != nullptr && thread_local_layout->image != nullptr &&
      thread_local_layout->image_size == sizeof(tls_initialized)) {
    memcpy(&initial_value, thread_local_layout->image, sizeof(initial_value));
  }

  const bool auxv_ready = __libuc_auxv_get(AT_PAGESZ, &page_size) &&
                          page_size == (uintptr_t)4096 &&
                          !__libuc_auxv_get(UINTPTR_MAX, &absent_value);
  const bool thread_local_layout_ready =
      thread_local_layout != nullptr && thread_local_layout_stable &&
      initial_value == 42 &&
      thread_local_layout->block_size ==
          sizeof(tls_initialized) + sizeof(tls_zeroed) &&
      thread_local_layout->alignment == alignof(int);

  const bool thread_local_state_ready =
      tls_initialized == 42 && tls_zeroed == 0;
  tls_zeroed = 7;

  stage = (stage == 0 && auxv_ready && thread_local_layout_ready &&
           thread_local_state_ready)
              ? 1
              : -1;
}

[[gnu::constructor(202)]] static void init_second(void) {
  stage = (stage == 1) ? 2 : -1;
}

[[gnu::constructor]] static void init_last(void) {
  stage = (stage == 2) ? 3 : -1;
}

#pragma clang diagnostic pop

int main(int argc, char **argv, char **envp) {
  if (stage != 3) {
    return 125;
  }

  /* The constructor's block is main's block. */
  if (tls_initialized != 42 || tls_zeroed != 7) {
    return 123;
  }

  if (argc < 1 || argv == nullptr || argv[0] == nullptr || envp == nullptr) {
    return 127;
  }

  const size_t argument_count = (size_t)argc;
  if (argv[argument_count] != nullptr || envp != &argv[argument_count + 1]) {
    return 126;
  }

  /* argv lives on the kernel-provided stack; a local farther than any
   * plausible bootstrap frame shows this frame is on some other mapping.
   * That the other mapping is the root fiber's follows from the control
   * flow, not from this distance check. */
  unsigned char stack_probe;
  const uintptr_t here = (uintptr_t)&stack_probe;
  const uintptr_t kernel_stack = (uintptr_t)argv;
  const uintptr_t main_distance =
      here > kernel_stack ? here - kernel_stack : kernel_stack - here;
  const uintptr_t constructor_distance =
      constructor_stack_witness > kernel_stack
          ? constructor_stack_witness - kernel_stack
          : kernel_stack - constructor_stack_witness;
  if (main_distance < ((uintptr_t)1 << 20) ||
      constructor_distance < ((uintptr_t)1 << 20)) {
    return 124;
  }

  return 0;
}
