/*
 * Crash handler bodies. Contracts and the verified ABI facts are in crash.h;
 * what lives here is the dump itself. Stubs trap until implemented.
 */

#include "crash.h"

/* TODO(6): The signal handler and the dump. Static — nothing outside this
 * file installs or calls it directly.
 *
 * What to print, in order of usefulness when the console is all you get:
 *
 *   1. The signal number and si_addr — the faulting address answers "null
 *      pointer, wild pointer, or guard page?" before anything else does.
 *   2. pc and pstate.
 *   3. x0-x30 and sp, fixed-width hex in columns (fmt.h's TODO(2) exists for
 *      this).
 *   4. The frame-pointer walk: x29 is fp; each frame is [fp] = saved fp,
 *      [fp+8] = saved lr. Bound it (64 frames) and validate every fp before
 *      dereferencing — aligned, nonnull, inside a known stack — because the
 *      handler must not fault while reporting a fault. It terminates on the
 *      zeroed x29 that RT-003's _start planted for exactly this moment.
 *   5. Which task was running and its stack range — but rt_current arrives
 *      with RT-006, so leave the seam marked and print it then.
 *
 * The trap the ucontext layout hides: uc_mcontext sits after glibc-compat
 * padding sized 1024/8 - sizeof(sigset_t) (asm/ucontext.h) — hand-computing
 * its offset is exactly the retyping invariant 4 forbids. Include the uapi
 * header and let the compiler place it.
 */

void rt_crash_install(void) { __builtin_trap(); }

[[noreturn]] void rt_panic(const char *what, void *where) {
  (void)what;
  (void)where;
  __builtin_trap();
}
