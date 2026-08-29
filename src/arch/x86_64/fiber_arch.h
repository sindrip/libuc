#ifndef LIBUC_SRC_ARCH_X86_64_FIBER_ARCH_H
#define LIBUC_SRC_ARCH_X86_64_FIBER_ARCH_H

#include <stddef.h>

struct fiber_arch_context {
  unsigned long rbx;
  unsigned long rbp;
  unsigned long r12;
  unsigned long r13;
  unsigned long r14;
  unsigned long r15;
  unsigned long rsp;
  unsigned long rip;
};

[[gnu::naked]] [[maybe_unused]] static void
fiber_arch_switch([[maybe_unused]] struct fiber_arch_context *save,
                  [[maybe_unused]] const struct fiber_arch_context *restore) {
  __asm__ volatile("movq %%rbx, %c[rbx](%%rdi)\n"
                   "movq %%rbp, %c[rbp](%%rdi)\n"
                   "movq %%r12, %c[r12](%%rdi)\n"
                   "movq %%r13, %c[r13](%%rdi)\n"
                   "movq %%r14, %c[r14](%%rdi)\n"
                   "movq %%r15, %c[r15](%%rdi)\n"
                   "movq (%%rsp), %%rax\n"
                   "movq %%rax, %c[rip](%%rdi)\n"
                   "leaq 8(%%rsp), %%rax\n"
                   "movq %%rax, %c[rsp](%%rdi)\n"

                   "movq %c[rbx](%%rsi), %%rbx\n"
                   "movq %c[rbp](%%rsi), %%rbp\n"
                   "movq %c[r12](%%rsi), %%r12\n"
                   "movq %c[r13](%%rsi), %%r13\n"
                   "movq %c[r14](%%rsi), %%r14\n"
                   "movq %c[r15](%%rsi), %%r15\n"
                   "movq %c[rsp](%%rsi), %%rsp\n"
                   "jmpq *%c[rip](%%rsi)\n"
                   :
                   : [rbx] "i"(offsetof(struct fiber_arch_context, rbx)),
                     [rbp] "i"(offsetof(struct fiber_arch_context, rbp)),
                     [r12] "i"(offsetof(struct fiber_arch_context, r12)),
                     [r13] "i"(offsetof(struct fiber_arch_context, r13)),
                     [r14] "i"(offsetof(struct fiber_arch_context, r14)),
                     [r15] "i"(offsetof(struct fiber_arch_context, r15)),
                     [rsp] "i"(offsetof(struct fiber_arch_context, rsp)),
                     [rip] "i"(offsetof(struct fiber_arch_context, rip))
                   : "memory");
}

#endif
