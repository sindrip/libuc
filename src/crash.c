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

void rt_crash_install(void) {
  /* a) The wrappers, sys_rt_sigaction and sys_sigaltstack, are in syscall.h;
   *    oldact is nullptr here, and sigsetsize is the wrapper's business. */

  /* b) The alternate stack, before any handler names it. A static buffer of
   *    SIGSTKSZ — which is 16384 on arm64 (asm/signal.h:24), NOT the generic
   *    8192 the man pages quote: every signal frame carries sigcontext's
   *    4096-byte __reserved block for SIMD/SVE state, so the generic size
   *    could lose half a stack to one frame. alignas(16): it hosts real
   *    frames. stack_t's ss_sp is the buffer's BASE — its lowest address —
   *    unlike the task stacks, the kernel computes the top itself. Per the
   *    header: this is per-thread state; milestone 3's workers install
   *    their own. */

  /* c) One struct sigaction, installed four times. The kernel's raw struct
   *    (asm-generic/signal.h:68) has NO sa_sigaction member — that union is
   *    a libc invention. SA_SIGINFO changes how the kernel CALLS the handler
   *    (three arguments), not where it is stored: the three-argument handler
   *    is cast into sa_handler, and the cast is safe because with SA_SIGINFO
   *    the kernel always passes all three. Flags are SA_SIGINFO | SA_ONSTACK;
   *    sa_restorer exists and stays zero with the flag unset (the VDSO fact
   *    in crash.h); sa_mask stays zero — the faulting signal is blocked
   *    automatically, and a handler that only dumps and halts needs nothing
   *    else masked. */

  /* d) Install for SIGSEGV, SIGBUS, SIGILL and SIGFPE, checking each
   *    return. These calls fail only on programmer error, so a failure at
   *    boot is a build bug: report through raw_write and trap — a runtime
   *    that believes it has a crash handler but does not is worse than one
   *    that refuses to start.
   *
   *    Prove firing before building the dump (the ticket's own advice): a
   *    minimal handler that raw_writes one line and loops, kicked by a
   *    deliberate null deref, validates the whole install — the restorer
   *    behaviour, the alternate stack, the cast — while the dump is still
   *    unwritten. */

  __builtin_trap();
}

[[noreturn]] void rt_panic(const char *what, void *where) {
  (void)what;
  (void)where;
  __builtin_trap();
}
