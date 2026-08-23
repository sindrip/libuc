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

#endif
