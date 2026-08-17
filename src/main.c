/*
 * The runtime's entry point, called from _start.
 *
 * At this milestone it does exactly two things: prove it is alive on the
 * console, and never return. That is the whole of RT-003 — no ring, no
 * coroutines, no allocator.
 *
 * Local compile check (no linker needed, and none is available on macOS):
 *
 *   clang --target=aarch64-unknown-linux-gnu -std=c23 -ffreestanding -O1 \
 *         -I out/uapi/include -c src/main.c -o /tmp/main.o
 */
#include "syscall.h"

/*
 * PID 1 must never return. [[noreturn]] is C23's spelling (the old _Noreturn
 * and <stdnoreturn.h> are deprecated), and it lets the compiler both check the
 * claim and drop the function epilogue.
 */
[[noreturn]] void rt_main(void *stack) {
  (void)stack; /* auxv lives here; we do not need it yet */

  /* TODO(1): Write a banner to fd 1.
   *
   * There is no printf, no puts, and no strlen — you have raw_write and
   * nothing else.
   *
   * Two ways to get the length, and the difference is worth understanding:
   *
   *   a) sizeof "rt: alive\n" - 1
   *      Compile-time. sizeof on a string *literal* includes the NUL
   *      terminator, hence the -1. Costs nothing at runtime.
   *
   *   b) a hand-written strlen loop
   *      Runtime. Necessary once the string is a `const char *` rather than
   *      a literal, because sizeof on a pointer gives you 8.
   *
   * Use (a) here. Understand why it stops working the moment the banner
   * becomes a variable.
   */
  constexpr int RT_CONSOLE = 1;
  static const char banner[] = "rt: alive\n";
  raw_write(RT_CONSOLE, banner, sizeof banner - 1);

  /* TODO(2): Never return.
   *
   * An empty `for (;;) {}` is undefined behaviour in C11 and later if the
   * loop has no side effects and no controlling expression that could become
   * false — the compiler is permitted to assume it terminates and may delete
   * it. This is a real optimisation, not a theoretical one.
   *
   * Give the loop an observable side effect so it cannot be removed. The
   * cheapest correct one is an empty volatile asm statement:
   *
   *     for (;;) __asm__ volatile("");
   *
   * Better still on aarch64, use the `wfe` (wait-for-event) instruction so
   * the core idles instead of spinning at 100%:
   *
   *     for (;;) __asm__ volatile("wfe");
   *
   * Once there is a scheduler this becomes the idle path — the point where a
   * core with no runnable tasks blocks in io_uring_enter instead.
   */
  for (;;) {
    __asm__ volatile("wfe");
  }
}
