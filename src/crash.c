#include "crash.h"

#include <stddef.h>
#include <stdint.h>

#include <asm/sigcontext.h>
#include <asm/siginfo.h>
#include <asm/signal.h>
#include <asm/ucontext.h>

#include "fmt.h"
#include "syscall.h"

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

static void on_fault(int sig, siginfo_t *info, void *ucv) {
  const struct ucontext *uc = ucv;
  const struct sigcontext *mc = &uc->uc_mcontext;

  char buf[96];
  struct rt_fmt f = {buf, buf + sizeof buf};

  rt_fmt_str(&f, "crash: sig ");
  rt_fmt_dec(&f, (unsigned long)sig);
  rt_fmt_str(&f, " addr ");
  rt_fmt_hex(&f, (unsigned long)(uintptr_t)info->si_addr);
  rt_fmt_str(&f, "\n");
  raw_write(1, buf, (size_t)(f.p - buf));

  f.p = buf;
  rt_fmt_str(&f, "  pc ");
  rt_fmt_hex(&f, (unsigned long)mc->pc);
  rt_fmt_str(&f, " pstate ");
  rt_fmt_hex(&f, (unsigned long)mc->pstate);
  rt_fmt_str(&f, "\n");
  raw_write(1, buf, (size_t)(f.p - buf));

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

  dump_frames((unsigned long)mc->regs[29]);

  for (;;) {
    __asm__ volatile("wfe");
  }
}

static const union {
  void (*siginfo)(int, siginfo_t *, void *);
  __sighandler_t handler;
} on_fault_ptr = {.siginfo = on_fault};

void rt_crash_install(void) {

  static alignas(16) char stack[SIGSTKSZ];

  const stack_t ss = {.ss_sp = stack, .ss_size = sizeof stack};
  auto ret = sys_sigaltstack(&ss, nullptr);
  if (sys_failed(ret)) {
    install_fail("sigaltstack", ret);
  }

  const struct sigaction act = {.sa_handler = on_fault_ptr.handler,
                                .sa_flags = SA_SIGINFO | SA_ONSTACK};

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
