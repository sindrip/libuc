/*
 * Raw Linux syscalls for aarch64. No libc, so this is the floor of the runtime:
 * everything the kernel can do for us that has no io_uring opcode comes through
 * here.
 *
 * ABI (aarch64):
 *   x8       syscall number
 *   x0..x5   arguments 0..5
 *   svc #0   trap into the kernel
 *   x0       return value
 *
 * There is no errno. A failure comes back as -errno in the range -1..-4095,
 * which is why sys_failed() checks a *range* and not just r < 0: a syscall like
 * mmap legitimately returns large values whose top bit is set.
 */
#ifndef RT_SYSCALL_H
#define RT_SYSCALL_H

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

/* ---------------------------------------------------------------------------
 * WORKED EXAMPLE — read this closely; you will write the others from it.
 *
 * Every token below is load-bearing:
 *
 *   register long x8 __asm__("x8")
 *       A GCC/Clang extension pinning a variable to a *named* register. Normal
 *       constraints can only say "some register"; the kernel ABI demands
 *       exactly x8, so we must be able to name it.
 *
 *   __asm__ volatile
 *       Without volatile the compiler may delete the asm when it believes the
 *       result is unused, or hoist it out of a loop. A syscall has effects the
 *       compiler cannot see, so it must never be treated as pure.
 *
 *   "+r"(x0)
 *       Read-write. x0 is both argument 0 and the return value. Writing "=r"
 *       would tell the compiler x0 is write-only, so it could skip setting up
 *       the input entirely.
 *
 *   "memory"
 *       The kernel may read or write our memory — write(2) reads the buffer we
 *       pass. Without this clobber the compiler is free to keep that buffer's
 *       contents in registers and never store them, so the kernel reads stale
 *       memory. This is the classic bug that works at -O0 and breaks at -O1.
 *
 *   "cc"
 *       Condition flags may be clobbered by the trap.
 * ------------------------------------------------------------------------- */
static inline long sys3(long nr, long a0, long a1, long a2) {
  register long x8 __asm__("x8") = nr;
  register long x0 __asm__("x0") = a0;
  register long x1 __asm__("x1") = a1;
  register long x2 __asm__("x2") = a2;

  __asm__ volatile("svc #0"
                   : "+r"(x0)
                   : "r"(x8), "r"(x1), "r"(x2)
                   : "memory", "cc");
  return x0;
}

/* TODO(1): sys1 — one argument. Used by exit_group.
 *
 * Same shape as sys3, minus x1 and x2. Watch the constraint on x0.
 */
static inline long sys1(long nr, long a0) {
  register long x8 __asm__("x8") = nr;
  register long x0 __asm__("x0") = a0;

  __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory", "cc");

  return x0;
}

/* TODO [RT-005]: sys2 — two arguments. Used by io_uring_setup, which is
 * SYSCALL_DEFINE2(entries, params) at io_uring.c:3145.
 *
 * Same shape as sys3, minus x2.
 */
static inline long sys2(long nr, long a0, long a1) {
  register long x8 __asm__("x8") = nr;
  register long x0 __asm__("x0") = a0;
  register long x1 __asm__("x1") = a1;

  __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1) : "memory", "cc");
  return x0;
}

/* TODO [RT-005]: sys4 — four arguments. Used by io_uring_register,
 * SYSCALL_DEFINE4(fd, opcode, arg, nr_args) at register.c:1016.
 */
static inline long sys4(long nr, long a0, long a1, long a2, long a3) {
  register long x8 __asm__("x8") = nr;
  register long x0 __asm__("x0") = a0;
  register long x1 __asm__("x1") = a1;
  register long x2 __asm__("x2") = a2;
  register long x3 __asm__("x3") = a3;

  __asm__ volatile("svc #0"
                   : "+r"(x0)
                   : "r"(x8), "r"(x1), "r"(x2), "r"(x3)
                   : "memory", "cc");

  return x0;
}

/* TODO [RT-004]: sys6 — six arguments. Used by mmap.
 *
 * Same shape as sys3, extended with x3, x4, x5 pinned to their registers
 * and listed as inputs. x0 stays the read-write operand.
 *
 * Six is the maximum: aarch64 passes syscall arguments in x0–x5 and
 * nothing more, so this is the widest wrapper the ABI can need. mmap is
 * why it exists — addr, len, prot, flags, fd, offset. mprotect takes
 * three, so sys3 already covers it.
 *
 * The gaps are deliberate: add a wrapper when a syscall needs one.
 */
static inline long sys6(long nr, long a0, long a1, long a2, long a3, long a4,
                        long a5) {
  register long x8 __asm__("x8") = nr;
  register long x0 __asm__("x0") = a0;
  register long x1 __asm__("x1") = a1;
  register long x2 __asm__("x2") = a2;
  register long x3 __asm__("x3") = a3;
  register long x4 __asm__("x4") = a4;
  register long x5 __asm__("x5") = a5;

  __asm__ volatile("svc #0"
                   : "+r"(x0)
                   : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
                   : "memory", "cc");

  return x0;
}

/* TODO(2): sys_failed — did a raw syscall return fail?
 *
 * Raw returns are -errno in -1..-4095. Anything else is a success value, and
 * some of those are huge (mmap returns an address). Return true only for the
 * error range.
 *
 * static inline bool sys_failed(long r) { ... }
 */
static inline bool sys_failed(long r) { return r < 0 && r >= -4095; }

/* TODO(3): raw_write — THE sanctioned purity exception.
 *
 * A direct write(2) to a file descriptor. This is the only deliberate
 * direct-syscall I/O in the project, and it exists for three reasons:
 *   - nothing before RT-005 has a ring to write through;
 *   - io_uring_setup failure must be reportable, and cannot be reported via
 *     the ring that just failed to exist;
 *   - the crash handler may fire with the ring in an unknown state.
 *
 * Once RT-006 lands, all normal output goes through IORING_OP_WRITE instead.
 * If you find yourself reaching for this anywhere else, that is the signal to
 * stop and reread invariant 1.
 *
 * static inline long raw_write(int fd, const void *buf, size_t len)
 * {
 *     return sys3(__NR_write, ...);
 * }
 *
 * Note the casts: sys3 takes long, and you are handed a pointer and a size.
 * Think about which cast is correct for a pointer -> long conversion and why
 * (uintptr_t exists in <stdint.h>, which is freestanding).
 *
 * Deliberately not [[nodiscard]], alone among the wrappers: the put family
 * ignores console-write failures because PID 1 has no recourse when the
 * console is gone — there is nowhere else to report.
 */
static inline long raw_write(int fd, const void *buf, size_t len) {
  return sys3(__NR_write, fd, (long)(uintptr_t)buf, (long)len);
}

/* TODO(4): sys_exit_group — terminate the whole process.
 *
 * PID 1 must never reach this on the happy path; it exists so that a fall
 * through _start is loud rather than undefined.
 *
 * Mark it [[noreturn]] and make sure the compiler believes you — after the
 * syscall the function must not fall off the end. C23 standardized the way to
 * say so: unreachable(), from <stddef.h>, freestanding and already included.
 *
 * That is a kernel guarantee, not an assumption: do_group_exit is declared
 * __noreturn (include/linux/sched/task.h:93) and the syscall body is marked
 * NOTREACHED (kernel/exit.c:1161).
 */
[[noreturn]] static inline void sys_exit_group(int status) {
  sys1(__NR_exit_group, status);

  unreachable();
}

/* TODO [RT-004]: sys_mmap — anonymous memory.
 *
 * Returns the mapped address, or -errno in the sys_failed() range. Note
 * the return type: long, not void *. A pointer cannot represent -ENOMEM,
 * so the check has to happen before the caller converts.
 *
 * fd is the trap. mmap's argument is a long, and MAP_ANONYMOUS wants -1;
 * a bare -1 int sign-extends correctly only if you let it — pass it
 * through (long) explicitly here so no caller has to think about it.
 *
 * Direct syscall, not a ring op: mmap has no io_uring opcode, which is
 * why invariant 1 lists it as permitted.
 */
[[nodiscard]] static inline long sys_mmap(void *addr, size_t len, int prot,
                                          int flags, int fd,
                                          unsigned long off) {
  return sys6(__NR_mmap, (long)(uintptr_t)addr, (long)len, prot, flags, fd,
              (long)off);
}

/* TODO [RT-004]: sys_mprotect — change protection on an existing mapping.
 *
 * Three arguments, so sys3 covers it. Used to open the usable part of a
 * task stack after mapping the whole region PROT_NONE.
 */
[[nodiscard]] static inline int sys_mprotect(void *addr, size_t len, int prot) {
  return (int)sys3(__NR_mprotect, (long)(uintptr_t)addr, (long)len, prot);
}

/* Incomplete type: enough to declare a pointer parameter, and it keeps a
 * thousand lines of io_uring uapi out of every file that only wants write(2).
 * ring.c includes the real header. */
struct io_uring_params;

/* TODO [RT-005]: the three io_uring syscalls.
 *
 * These are the only syscalls the runtime makes directly once the ring is up;
 * everything with an opcode goes through the ring instead (invariant 1).
 * Typed parameters with the casts inside, like raw_write and sys_mmap.
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
  return (int)sys2(__NR_io_uring_setup, entries, (long)(uintptr_t)p);
}

[[nodiscard]] static inline int
sys_io_uring_enter(int fd, unsigned to_submit, unsigned min_complete,
                   unsigned flags, const void *arg, size_t argsz) {
  return (int)sys6(__NR_io_uring_enter, fd, to_submit, min_complete, flags,
                   (long)(uintptr_t)arg, (long)argsz);
}

[[nodiscard]] static inline int
sys_io_uring_register(int fd, unsigned opcode, void *arg, unsigned nr_args) {
  return (int)sys4(__NR_io_uring_register, fd, opcode, (long)(uintptr_t)arg,
                   nr_args);
}

/* TODO [RT-007]: sys_rt_sigaction and sys_sigaltstack — the crash handler's
 * two installs. Direct syscalls: neither has an opcode (invariant 1's list).
 * Both return 0 or -errno in sys_failed()'s range.
 *
 * rt_sigaction is sys4-shaped: (signum, act, oldact, sigsetsize). Two
 * decisions belong inside the wrapper, following sys_mmap's fd precedent —
 * quirks live here so no caller has to think about them:
 *
 *   - sigsetsize is not a parameter of the wrapper: the kernel accepts
 *     exactly sizeof(sigset_t) and nothing else (kernel/signal.c:4648), so
 *     the wrapper supplies it. A wrapper that lets callers pass it is a
 *     wrapper that lets callers get it wrong.
 *   - oldact stays a parameter (nullable) — reading back the old action is
 *     legitimate, just unused today.
 *
 * sigaltstack is sys2-shaped: (ss, old_ss), old_ss nullable.
 */
[[nodiscard]] static inline int sys_rt_sigaction(int signum,
                                                 const struct sigaction *act,
                                                 struct sigaction *oldact) {
  return (int)sys4(__NR_rt_sigaction, signum, (long)(uintptr_t)act,
                   (long)(uintptr_t)oldact, (long)sizeof(sigset_t));
}

[[nodiscard]] static inline int sys_sigaltstack(const struct sigaltstack *ss,
                                                struct sigaltstack *old_ss) {
  return (int)sys2(__NR_sigaltstack, (long)(uintptr_t)ss,
                   (long)(uintptr_t)old_ss);
}

#endif /* RT_SYSCALL_H */
