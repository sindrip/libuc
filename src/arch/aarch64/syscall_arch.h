/*
 * The aarch64 syscall floor: how a syscall physically happens on this
 * machine. This file knows registers, not policy — which syscalls may be
 * made, and with what types, is syscall.h's business.
 *
 * Name and place follow prior art rather than invention: musl keeps this
 * exact file, near line for line, at arch/aarch64/syscall_arch.h; the kernel
 * and glibc use the same layout rule (arch/, sysdeps/). Architecture is a
 * path, not a suffix — a second architecture someday means a sibling
 * directory, and syscall.h does not change.
 *
 * ABI:
 *   x8       syscall number
 *   x0..x5   arguments 0..5
 *   svc #0   trap into the kernel
 *   x0       return value
 *
 * Every token in the dispatchers is load-bearing:
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
 *
 * The gaps in the arity family are deliberate: a dispatcher is added when a
 * syscall first needs it, and six is the maximum — aarch64 passes syscall
 * arguments in x0-x5 and nothing more.
 */
#ifndef RT_SYSCALL_ARCH_H
#define RT_SYSCALL_ARCH_H

static inline long syscall1(long nr, long a0) {
  register long x8 __asm__("x8") = nr;
  register long x0 __asm__("x0") = a0;

  __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory", "cc");

  return x0;
}

static inline long syscall2(long nr, long a0, long a1) {
  register long x8 __asm__("x8") = nr;
  register long x0 __asm__("x0") = a0;
  register long x1 __asm__("x1") = a1;

  __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1) : "memory", "cc");
  return x0;
}

static inline long syscall3(long nr, long a0, long a1, long a2) {
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

static inline long syscall4(long nr, long a0, long a1, long a2, long a3) {
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

static inline long syscall6(long nr, long a0, long a1, long a2, long a3,
                            long a4, long a5) {
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

#endif /* RT_SYSCALL_ARCH_H */
