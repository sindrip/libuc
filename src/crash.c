/*
 * Crash handler bodies. Contracts and the verified ABI facts are in crash.h;
 * what lives here is the dump itself. Stubs trap until implemented.
 */

#include "crash.h"

#include <asm/sigcontext.h>
#include <asm/siginfo.h>
#include <asm/signal.h>
#include <asm/ucontext.h>

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

/* The bounded frame-pointer walk, shared between on_fault (seeded from the
 * interrupted context's regs[29]) and rt_panic (seeded from its own
 * __builtin_frame_address(0)). One "  lr <hex>" line per frame.
 *
 * Each frame record is [fp] = saved fp, [fp+8] = saved lr. Every fp is
 * validated BEFORE the two loads: nonnull, aligned for the loads (alignof,
 * matching the kernel unwinder's fp & 0x7 at stacktrace.c:224 — stricter
 * would silently truncate genuine walks), and strictly greater
 * than where it came from — stacks grow down, so walking toward older frames
 * means addresses must rise, and monotonicity both breaks any cycle a
 * corrupted chain could form and guarantees termination. Clean termination
 * is the zero fp that _start planted (arch/aarch64/start.S) for exactly this
 * moment; every other stop reason is also just a stop.
 *
 * Deliberately no frame-count cap: the oldest frames are the diagnosis — in
 * a runaway recursion, the entry point that started it prints LAST — and
 * genuine chains are already bounded by the geometry of the stack they live
 * in. A corrupt chain can still walk somewhere unmapped and fault mid-dump;
 * the one-raw_write-per-line policy means every frame already printed has
 * escaped, and the death is loud. Real prevention is a range check against
 * known stacks, the kernel unwinder's design (stacktrace.c:227) — that
 * arrives with RT-006's task registry. */
static void dump_frames(unsigned long fp) {
  while (fp != 0 && fp % alignof(unsigned long) == 0) {
    const unsigned long *frame = (const unsigned long *)fp;

    char buf[32];
    struct rt_fmt f = {buf, buf + sizeof buf};
    rt_fmt_str(&f, "  lr ");
    rt_fmt_hex(&f, frame[1]);
    rt_fmt_str(&f, "\n");
    raw_write(1, buf, (size_t)(f.p - buf));

    const unsigned long next = frame[0];
    if (next <= fp) {
      break;
    }

    fp = next;
  }
}

/* The handler. Never returns — returning re-executes the faulting
 * instruction. raw_write only (the purity exception's third charter reason:
 * the ring may be exactly what is broken); the halt loop, not exit_group,
 * preserves the scene for ./debug.sh.
 *
 * Now three arguments, so the dump can reach the interrupted context. The
 * third is void * by convention; it points at struct ucontext
 * (asm/ucontext.h), whose LAST field — after the glibc-compat padding — is
 * uc_mcontext, a struct sigcontext: fault_address, regs[31], sp, pc, pstate
 * (asm/sigcontext.h). Include the header and let the compiler place the
 * offset; hand-computing it is the retyping invariant 4 forbids.
 *
 * The dump, one raw_write per line so a mid-dump failure still leaves the
 * earlier lines on the console:
 *
 *   a) What happened: "crash: sig <n> addr <hex>". si_addr answers "null
 *      pointer, wild pointer, or guard page?" before anything else does —
 *      the uapi spells it via the si_addr convenience macro
 *      (asm-generic/siginfo.h).
 *   b) Where: pc and pstate, one line.
 *   c) The machine: x0-x30 from regs[31] and then sp, fixed-width hex — the
 *      column format rt_fmt_hex was designed for. A few registers per line;
 *      pick a count that survives an 80-column console.
 *   d) How we got there: dump_frames(regs[29]) — x29 is fp.
 *   e) The seam: which task was running and its stack range, when
 *      rt_current arrives with RT-006.
 *
 * Then the halt loop. */
static void on_fault(int sig, siginfo_t *info, void *ucv) {
  const struct ucontext *uc = ucv;
  const struct sigcontext *mc = &uc->uc_mcontext;

  char buf[96];
  struct rt_fmt f = {buf, buf + sizeof buf};

  /* a) What happened. */
  rt_fmt_str(&f, "crash: sig ");
  rt_fmt_dec(&f, (unsigned long)sig);
  rt_fmt_str(&f, " addr ");
  rt_fmt_hex(&f, (unsigned long)(uintptr_t)info->si_addr);
  rt_fmt_str(&f, "\n");
  raw_write(1, buf, (size_t)(f.p - buf));

  /* b) Where. */
  f.p = buf;
  rt_fmt_str(&f, "  pc ");
  rt_fmt_hex(&f, (unsigned long)mc->pc);
  rt_fmt_str(&f, " pstate ");
  rt_fmt_hex(&f, (unsigned long)mc->pstate);
  rt_fmt_str(&f, "\n");
  raw_write(1, buf, (size_t)(f.p - buf));

  /* c) The machine, three registers per line; the label pad keeps the hex
   *    columns aligned across one- and two-digit indices. */
  for (int i = 0; i < 31; i += 3) {
    f.p = buf;
    for (int j = i; j < i + 3 && j < 31; j++) {
      rt_fmt_str(&f, " x");
      rt_fmt_dec(&f, (unsigned long)j);
      rt_fmt_str(&f, j < 10 ? "  " : " ");
      rt_fmt_hex(&f, (unsigned long)mc->regs[j]);
    }
    rt_fmt_str(&f, "\n");
    raw_write(1, buf, (size_t)(f.p - buf));
  }
  f.p = buf;
  rt_fmt_str(&f, " sp  ");
  rt_fmt_hex(&f, (unsigned long)mc->sp);
  rt_fmt_str(&f, "\n");
  raw_write(1, buf, (size_t)(f.p - buf));

  /* d) How we got there — x29 is fp. (e: the running task and its stack
   *    range land here with RT-006.) */
  dump_frames((unsigned long)mc->regs[29]);

  for (;;) {
    __asm__ volatile("wfe");
  }
}

/* SA_SIGINFO changes how the kernel CALLS the handler, not where it is
 * stored: the kernel's struct sigaction has only sa_handler, a
 * void (*)(int). Storing the three-argument handler therefore needs a
 * reinterpretation, and the union performs it without a function-pointer
 * cast — which -Wcast-function-type-strict would (rightly) flag, since a
 * cast between incompatible function types is exactly what this is. The
 * union states the dual calling convention as a type instead of smuggling
 * it through a cast; musl's kernel-sigaction fill does the same. All
 * function pointers share one representation on aarch64, and the kernel
 * calls through the three-argument type because SA_SIGINFO says so. */
static const union {
  void (*siginfo)(int, siginfo_t *, void *);
  __sighandler_t handler;
} on_fault_ptr = {.siginfo = on_fault};

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
   *    sa_handler takes the union-reinterpreted on_fault (see on_fault_ptr):
   *    the kernel's raw struct (asm-generic/signal.h:68) has NO sa_sigaction
   *    member — that union is a libc invention. Flags are SA_SIGINFO |
   *    SA_ONSTACK; the designated initializer zeroes the rest, including
   *    sa_restorer (must exist, must be zero — the VDSO fact in crash.h) and
   *    sa_mask (the faulting signal is blocked automatically; a handler that
   *    only dumps and halts needs nothing else masked). The trap this step
   *    guards: a zeroed sa_handler is SIG_DFL, so an empty act "succeeds"
   *    while installing the frozen-VM status quo. */
  const struct sigaction act = {.sa_handler = on_fault_ptr.handler,
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

/* The shared dump-and-halt for non-signal callers — the UBSan handlers, and
 * anything else that discovers corruption without a fault. There is no
 * ucontext here, so the dump is smaller: the panic line names the caller's
 * `where` (its __builtin_return_address(0), resolvable against the binary
 * since -no-pie), and the walk is seeded from this frame's own fp, so the
 * frames show how execution arrived. The same wfe halt as on_fault, for the
 * same reason: preserve the scene. */
[[noreturn]] void rt_panic(const char *what, void *where) {
  char buf[96];
  struct rt_fmt f = {buf, buf + sizeof buf};

  rt_fmt_str(&f, "panic: ");
  rt_fmt_str(&f, what);
  rt_fmt_str(&f, " at ");
  rt_fmt_hex(&f, (unsigned long)(uintptr_t)where);
  rt_fmt_str(&f, "\n");
  raw_write(1, buf, (size_t)(f.p - buf));

  void *fp = __builtin_frame_address(0);
  dump_frames((unsigned long)(uintptr_t)fp);

  for (;;) {
    __asm__ volatile("wfe");
  }
}
