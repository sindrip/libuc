/*
 * Crash reporting: signal handlers, register dump, frame-pointer walk. With
 * no tests, no core dumps and no shell, a good failure report is the only
 * diagnostic this project has (AGENTS.md). The three highest-risk components
 * — hand-rolled switch.S, hand-written ring index arithmetic, raw mmap'd
 * stacks — all fail as memory corruption, and without this a corrupted
 * context switch presents as a frozen VM.
 *
 * ABI facts, verified against the pinned tree:
 *
 *   - rt_sigaction is 134 and sigaltstack is 132 (asm-generic/unistd.h:366,
 *     370). Both sit on invariant 1's direct-syscall list: neither has an
 *     opcode.
 *   - arm64 defines SA_RESTORER (asm/signal.h:21), so the raw struct
 *     sigaction (asm-generic/signal.h:68-73) carries the sa_restorer field —
 *     it exists and must be zero. The flag itself stays unset: the kernel
 *     then supplies the VDSO's sigtramp as the return trampoline
 *     (arch/arm64/kernel/signal.c:1481-1484). No hand-rolled restorer.
 *   - rt_sigaction's last argument, sigsetsize, must be 8: _NSIG is 64
 *     (asm-generic/signal.h:7) and the kernel rejects any other size
 *     (kernel/signal.c:4648).
 *   - The handler's third argument points at struct ucontext
 *     (asm/ucontext.h): uc_mcontext is the *last* field, after glibc-compat
 *     padding, and holds fault_address, regs[31], sp, pc, pstate
 *     (asm/sigcontext.h).
 */
#ifndef RT_CRASH_H
#define RT_CRASH_H

/* Install the handlers. Called from rt_main before anything that
 * can fault — a handler installed after the crash reports nothing.
 *
 * sigaltstack first, then rt_sigaction for SIGSEGV, SIGBUS, SIGILL and
 * SIGFPE with SA_SIGINFO | SA_ONSTACK. The order is the point: SA_ONSTACK
 * with no alternate stack behind it is an empty promise, and the
 * stack-overflow fault — the one most worth reporting — needs the handler
 * to run somewhere that is not the stack that just overflowed.
 *
 * The alternate stack is per-thread state: a later worker thread must
 * install its own before any task runs on it, or a worker-core overflow
 * loses exactly this report.
 */
void rt_crash_install(void);

/* The shared dump-and-halt. The UBSan handlers call this too, so it
 * takes only what they have: a name and one address.
 *
 * Output through raw_write alone, per the purity registry's charter: the
 * ring may be exactly what is broken, and a handler that submits an SQE to
 * report a corrupted ring hangs instead of reporting.
 *
 * Never returns. A tight loop, not exit_group: the halt preserves the scene
 * for ./debug.sh.
 */
[[noreturn]] void rt_panic(const char *what, void *where);

#endif /* RT_CRASH_H */
