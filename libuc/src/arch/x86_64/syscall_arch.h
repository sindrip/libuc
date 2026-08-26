#ifndef LIBUC_SRC_ARCH_X86_64_SYSCALL_ARCH_H
#define LIBUC_SRC_ARCH_X86_64_SYSCALL_ARCH_H

[[nodiscard]] static inline long __libuc_syscall2(long number, long argument_0,
                                                  long argument_1) {
  register long rax __asm__("rax") = number;
  register long rdi __asm__("rdi") = argument_0;
  register long rsi __asm__("rsi") = argument_1;

  __asm__ volatile("syscall"
                   : "+r"(rax)
                   : "r"(rdi), "r"(rsi)
                   : "rcx", "r11", "memory", "cc");

  return rax;
}

[[nodiscard]] static inline long
__libuc_syscall6(long number, long argument_0, long argument_1, long argument_2,
                 long argument_3, long argument_4, long argument_5) {
  register long rax __asm__("rax") = number;
  register long rdi __asm__("rdi") = argument_0;
  register long rsi __asm__("rsi") = argument_1;
  register long rdx __asm__("rdx") = argument_2;
  register long r10 __asm__("r10") = argument_3;
  register long r8 __asm__("r8") = argument_4;
  register long r9 __asm__("r9") = argument_5;

  __asm__ volatile("syscall"
                   : "+r"(rax)
                   : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10), "r"(r8), "r"(r9)
                   : "rcx", "r11", "memory", "cc");

  return rax;
}

#endif
