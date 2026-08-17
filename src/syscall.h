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
#include <stdint.h>

#include <asm/unistd.h>

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
 * static inline long raw_write(int fd, const void *buf, unsigned long len)
 * {
 *     return sys3(__NR_write, ...);
 * }
 *
 * Note the casts: sys3 takes long, and you are handed a pointer and a size.
 * Think about which cast is correct for a pointer -> long conversion and why
 * (uintptr_t exists in <stdint.h>, which is freestanding).
 */
static inline long raw_write(int fd, const void *buf, unsigned long len) {

  return sys3(__NR_write, fd, (long)(uintptr_t)buf, (long)len);
}

/* TODO(4): sys_exit_group — terminate the whole process.
 *
 * PID 1 must never reach this on the happy path; it exists so that a fall
 * through _start is loud rather than undefined.
 *
 * Mark it [[noreturn]] and make sure the compiler believes you — after the
 * syscall the function must not fall off the end. __builtin_unreachable() is
 * the usual way to say so.
 */
[[noreturn]] static inline void sys_exit_group(int status) {
  sys1(__NR_exit_group, status);

  __builtin_unreachable();
}

#endif /* RT_SYSCALL_H */
