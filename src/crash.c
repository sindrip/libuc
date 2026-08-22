/*
 * Crash handler bodies. Contracts and the verified ABI facts are in crash.h;
 * what lives here is the dump itself. Stubs trap until implemented.
 */

#include "crash.h"

#include "fmt.h"
#include "syscall.h"

/* Install failure is a build bug wearing a runtime error: report the decoded
 * errno and refuse to start. A runtime that believes it has a crash handler
 * but does not will one day fault silently — the exact outcome this file
 * exists to prevent. */
[[noreturn]] static void install_fail(const char *what, int err) {
  char buf[64];
  struct rt_fmt f = {buf, buf + sizeof buf};

  rt_fmt_str(&f, "crash: ");
  rt_fmt_str(&f, what);
  rt_fmt_str(&f, " failed: ");
  rt_fmt_dec(&f, (unsigned long)-err);
  rt_fmt_str(&f, "\n");

  raw_write(1, buf, (size_t)(f.p - buf));
  __builtin_trap();
}

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

/* The handler, minimal until TODO(6) grows the dump. Its two jobs: prove
 * firing, and never return — returning re-executes the faulting instruction.
 * Declared as void (*)(int), which is exactly __sighandler_t, so no cast is
 * needed yet: SA_SIGINFO makes the kernel pass three arguments, and a
 * one-parameter function ignoring x1 and x2 is ABI-clean on aarch64. The
 * cast question arrives with the ucontext, at TODO(6).
 *
 * raw_write from here is the purity exception's third charter reason: the
 * ring may be exactly what is broken. The halt loop, not exit_group,
 * preserves the scene for ./debug.sh. */
static void on_fault(int sig) {
  char buf[32];
  struct rt_fmt f = {buf, buf + sizeof buf};

  rt_fmt_str(&f, "crash: sig ");
  rt_fmt_dec(&f, (unsigned long)sig);
  rt_fmt_str(&f, "\n");
  raw_write(1, buf, (size_t)(f.p - buf));

  for (;;) {
    __asm__ volatile("wfe");
  }
}

void rt_crash_install(void) {
  /* a) The wrappers, sys_rt_sigaction and sys_sigaltstack, are in syscall.h;
   *    oldact is nullptr here, and sigsetsize is the wrapper's business. */

  /* b) The alternate stack, before any sigaction: SA_ONSTACK in c) is an
   *    empty promise until this call registers the buffer. SIGSTKSZ is 16384
   *    on arm64 (asm/signal.h:24), NOT the generic 8192 the man pages quote —
   *    every signal frame carries sigcontext's 4096-byte __reserved block for
   *    SIMD/SVE state, so the generic size could lose half a stack to one
   *    frame. Fill a stack_t: ss_sp is the buffer's BASE — its lowest
   *    address; the kernel computes the top itself, unlike the task stacks —
   *    ss_size the full size, ss_flags zero. Check through install_fail. Per
   *    the header: per-thread state; milestone 3's workers install their
   *    own. */
  static alignas(16) char stack[SIGSTKSZ];

  const stack_t ss = {.ss_sp = stack, .ss_size = sizeof stack};
  int ret = sys_sigaltstack(&ss, nullptr);
  if (sys_failed(ret)) {
    install_fail("sigaltstack", ret);
  }

  /* c) One act, filled once, installed four times (the kernel copies it).
   *    sa_handler takes on_fault directly — the kernel's raw struct
   *    (asm-generic/signal.h:68) has NO sa_sigaction member; that union is a
   *    libc invention. Flags are SA_SIGINFO | SA_ONSTACK; the designated
   *    initializer zeroes the rest, including sa_restorer (must exist, must
   *    be zero — the VDSO fact in crash.h) and sa_mask (the faulting signal
   *    is blocked automatically; a handler that only dumps and halts needs
   *    nothing else masked). The trap this step guards: a zeroed sa_handler
   *    is SIG_DFL, so an empty act "succeeds" while installing the
   *    frozen-VM status quo. */
  const struct sigaction act = {.sa_handler = on_fault,
                                .sa_flags = SA_SIGINFO | SA_ONSTACK};

  /* d) The four installs: loop over a constexpr array of SIGSEGV, SIGBUS,
   *    SIGILL and SIGFPE — names from the uapi, never numbers — one checked
   *    call each through install_fail. These fail only on programmer error;
   *    a failure at boot is a build bug, and install_fail refuses to start.
   *    When this lands, the trailing trap below goes: a successful install
   *    must fall through and return, and the healthy boot looks boring.
   *
   *    Then prove firing before building the dump (the ticket's own advice):
   *    a deliberate null deref in rt_main validates the whole install — the
   *    VDSO restorer, the alternate stack, the handler call — while the dump
   *    is still unwritten. */
  static constexpr int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE};
  constexpr size_t nsignals = sizeof signals / sizeof signals[0];

  for (size_t i = 0; i < nsignals; i++) {
    ret = sys_rt_sigaction(signals[i], &act, nullptr);
    if (sys_failed(ret)) {
      install_fail("sigaction", ret);
    }
  }
}

[[noreturn]] void rt_panic(const char *what, void *where) {
  (void)what;
  (void)where;
  __builtin_trap();
}
