#ifndef LIBUC_SRC_ARCH_AARCH64_SYSCALL_ARCH_H
#define LIBUC_SRC_ARCH_AARCH64_SYSCALL_ARCH_H

[[nodiscard]] static inline long
__libuc_syscall6(long number, long argument_0, long argument_1, long argument_2,
                 long argument_3, long argument_4, long argument_5) {
  register long x8 __asm__("x8") = number;
  register long x0 __asm__("x0") = argument_0;
  register long x1 __asm__("x1") = argument_1;
  register long x2 __asm__("x2") = argument_2;
  register long x3 __asm__("x3") = argument_3;
  register long x4 __asm__("x4") = argument_4;
  register long x5 __asm__("x5") = argument_5;

  __asm__ volatile("svc #0"
                   : "+r"(x0)
                   : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
                   : "memory", "cc");

  return x0;
}

#endif
