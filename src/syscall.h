/*
 * The direct-syscall registry: every syscall this runtime is permitted to
 * make outside the ring, typed. Invariant 1 in code — if an operation has an
 * opcode it goes through the ring, so a definition added here claims either
 * "no opcode exists on this kernel" (cite the tree) or an entry in the
 * purity-exception registry (raw_write, alone). Auditing what bypasses the
 * ring means reading this file.
 *
 * There is no errno. A failure comes back as -errno in the range -1..-4095,
 * which is why sys_failed() checks a *range* and not just r < 0: a syscall
 * like mmap legitimately returns large values whose top bit is set.
 *
 * The machine half — registers, svc, the syscallN dispatchers — lives in
 * arch/aarch64/syscall_arch.h, next to start.S and switch.c: architecture is
 * a path, not a suffix, and this file does not change when a second one
 * arrives.
 */
#ifndef RT_SYSCALL_H
#define RT_SYSCALL_H

#include "arch/aarch64/syscall_arch.h"

/* Syscall numbers come from the pinned kernel via headers_install. Never
 * hardcode them: __NR_mmap in the raw tree is a macro over __NR3264_mmap that
 * resolves through a __BITS_PER_LONG branch. Let the preprocessor do it. */
#include <stddef.h>
#include <stdint.h>

#include <asm/unistd.h>
#include <asm/unistd_64.h>

/* Unlike the io_uring uapi (a thousand lines, kept out via an incomplete
 * type), the signal uapi is a couple hundred lines of macros and typedefs —
 * cheap enough to include for real, which is what lets sys_rt_sigaction say
 * sizeof(sigset_t) instead of a bare 8. */
#include <asm/signal.h>

/* Raw returns are -errno in -1..-4095. Anything else is a success value, and
 * some of those are huge (mmap returns an address). True only for the error
 * range. */
[[nodiscard]] static inline bool sys_failed(long r) {
  return r < 0 && r >= -4095;
}

/* raw_write — THE sanctioned purity exception: a direct write(2).
 *
 * Failure-path output only; everything else goes through IORING_OP_WRITE.
 * The charter:
 *   - io_uring_setup failure cannot be reported through the ring that just
 *     failed to exist;
 *   - the crash handler and rt_panic may fire with the ring mid-mutation,
 *     the scheduler broken, or no current fiber — a diagnostic channel must
 *     not depend on the subsystem it diagnoses;
 *   - the boot-time regression chain probes the ring itself, and reporting
 *     a ring probe's failure through the ring is circular.
 * Reaching for this anywhere else is the signal to stop and reread
 * invariant 1.
 *
 * Deliberately not [[nodiscard]], alone among the wrappers: the reporting
 * paths ignore console-write failures because PID 1 has no recourse when the
 * console is gone — there is nowhere else to report.
 */
static inline long raw_write(int fd, const void *buf, size_t len) {
  return syscall3(__NR_write, fd, (long)(uintptr_t)buf, (long)len);
}

/* sys_exit_group — terminate the whole process. PID 1 must never reach this
 * on the happy path; it exists so that a fall through _start is loud rather
 * than undefined.
 *
 * The unreachable() after the svc rests on a kernel guarantee, not an
 * assumption: do_group_exit is declared __noreturn
 * (include/linux/sched/task.h:93) and the syscall body is marked NOTREACHED
 * (kernel/exit.c:1161).
 */
[[noreturn]] static inline void sys_exit_group(int status) {
  syscall1(__NR_exit_group, status);

  unreachable();
}

/* sys_mmap — anonymous memory. No io_uring opcode exists (invariant 1's
 * permitted list).
 *
 * Returns the mapped address, or -errno in the sys_failed() range. The
 * return type is long, not void *: a pointer cannot represent -ENOMEM, so
 * the check has to happen before the caller converts.
 *
 * fd is the trap. mmap's argument is a long, and MAP_ANONYMOUS wants -1;
 * the explicit (long) inside the wrapper is what guarantees the sign
 * extension so no caller has to think about it.
 */
[[nodiscard]] static inline long sys_mmap(void *addr, size_t len, int prot,
                                          int flags, int fd,
                                          unsigned long off) {
  return syscall6(__NR_mmap, (long)(uintptr_t)addr, (long)len, prot, flags, fd,
                  (long)off);
}

/* sys_mprotect — change protection on an existing mapping. No opcode. */
[[nodiscard]] static inline int sys_mprotect(void *addr, size_t len, int prot) {
  return (int)syscall3(__NR_mprotect, (long)(uintptr_t)addr, (long)len, prot);
}

/* Incomplete type: enough to declare a pointer parameter, and it keeps a
 * thousand lines of io_uring uapi out of every file that only wants write(2).
 * ring.c includes the real header. */
struct io_uring_params;

/* The three io_uring syscalls — the only ones the runtime makes directly
 * once the ring is up; everything with an opcode goes through the ring
 * instead (invariant 1).
 *
 * setup returns a ring fd, enter returns how many SQEs the kernel consumed,
 * register returns 0 — all three report failure in sys_failed()'s range.
 *
 * register's fd is signed rather than unsigned for a reason: -1 selects the
 * "blind" path that needs no ring at all (register.c:1031), which is how the
 * capability probe runs before setup has been called.
 */
[[nodiscard]] static inline int sys_io_uring_setup(unsigned entries,
                                                   struct io_uring_params *p) {
  return (int)syscall2(__NR_io_uring_setup, entries, (long)(uintptr_t)p);
}

[[nodiscard]] static inline int
sys_io_uring_enter(int fd, unsigned to_submit, unsigned min_complete,
                   unsigned flags, const void *arg, size_t argsz) {
  return (int)syscall6(__NR_io_uring_enter, fd, to_submit, min_complete, flags,
                       (long)(uintptr_t)arg, (long)argsz);
}

[[nodiscard]] static inline int
sys_io_uring_register(int fd, unsigned opcode, void *arg, unsigned nr_args) {
  return (int)syscall4(__NR_io_uring_register, fd, opcode, (long)(uintptr_t)arg,
                       nr_args);
}

/* sys_rt_sigaction and sys_sigaltstack — the crash handler's two installs.
 * Direct syscalls: neither has an opcode. Both return 0 or -errno in
 * sys_failed()'s range.
 *
 * rt_sigaction is syscall4-shaped: (signum, act, oldact, sigsetsize).
 * sigsetsize is not a parameter of the wrapper: the kernel accepts exactly
 * sizeof(sigset_t) and nothing else (kernel/signal.c:4648), so the wrapper
 * supplies it — a wrapper that lets callers pass it is a wrapper that lets
 * callers get it wrong. oldact stays a parameter (nullable): reading back
 * the old action is legitimate, just unused today.
 *
 * sigaltstack is syscall2-shaped: (ss, old_ss), old_ss nullable.
 */
[[nodiscard]] static inline int sys_rt_sigaction(int signum,
                                                 const struct sigaction *act,
                                                 struct sigaction *oldact) {
  return (int)syscall4(__NR_rt_sigaction, signum, (long)(uintptr_t)act,
                       (long)(uintptr_t)oldact, (long)sizeof(sigset_t));
}

[[nodiscard]] static inline int sys_sigaltstack(const struct sigaltstack *ss,
                                                struct sigaltstack *old_ss) {
  return (int)syscall2(__NR_sigaltstack, (long)(uintptr_t)ss,
                       (long)(uintptr_t)old_ss);
}

#endif /* RT_SYSCALL_H */
