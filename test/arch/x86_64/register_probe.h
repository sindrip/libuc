#ifndef LIBUC_TEST_ARCH_X86_64_REGISTER_PROBE_H
#define LIBUC_TEST_ARCH_X86_64_REGISTER_PROBE_H

#include "fiber/fiber.h"

/* Trash every callee-saved register and suspend before any epilogue can
 * repair them. */
[[gnu::naked]] [[maybe_unused]] static void dirty_registers_and_yield(void) {
  __asm__ volatile("pushq %%rbx\n"
                   "pushq %%rbp\n"
                   "pushq %%r12\n"
                   "pushq %%r13\n"
                   "pushq %%r14\n"
                   "pushq %%r15\n"
                   "pushq %%rax\n"

                   "movq $0xdead, %%rbx\n"
                   "movq $0xdead, %%rbp\n"
                   "movq $0xdead, %%r12\n"
                   "movq $0xdead, %%r13\n"
                   "movq $0xdead, %%r14\n"
                   "movq $0xdead, %%r15\n"
                   "callq __libuc_fiber_yield\n"

                   "popq %%rax\n"
                   "popq %%r15\n"
                   "popq %%r14\n"
                   "popq %%r13\n"
                   "popq %%r12\n"
                   "popq %%rbp\n"
                   "popq %%rbx\n"
                   "ret\n" ::);
}

/* Load a distinct sentinel into every callee-saved register, resume the
 * fiber through one suspension, and report the first register that came
 * back changed: 1..6 for rbx, rbp, r12..r15, 0 when all survived. */
[[gnu::naked]] [[maybe_unused]] static unsigned long
run_probing_registers(struct __libuc_fiber *fiber) {
  __asm__ volatile("pushq %%rbx\n"
                   "pushq %%rbp\n"
                   "pushq %%r12\n"
                   "pushq %%r13\n"
                   "pushq %%r14\n"
                   "pushq %%r15\n"
                   "pushq %%rdi\n"

                   "movq $0xb001, %%rbx\n"
                   "movq $0xb002, %%rbp\n"
                   "movq $0xb003, %%r12\n"
                   "movq $0xb004, %%r13\n"
                   "movq $0xb005, %%r14\n"
                   "movq $0xb006, %%r15\n"

                   "movq (%%rsp), %%rdi\n"
                   "callq __libuc_fiber_resume\n"

                   "movl $1, %%eax\n"
                   "cmpq $0xb001, %%rbx\n"
                   "jne 9f\n"
                   "movl $2, %%eax\n"
                   "cmpq $0xb002, %%rbp\n"
                   "jne 9f\n"
                   "movl $3, %%eax\n"
                   "cmpq $0xb003, %%r12\n"
                   "jne 9f\n"
                   "movl $4, %%eax\n"
                   "cmpq $0xb004, %%r13\n"
                   "jne 9f\n"
                   "movl $5, %%eax\n"
                   "cmpq $0xb005, %%r14\n"
                   "jne 9f\n"
                   "movl $6, %%eax\n"
                   "cmpq $0xb006, %%r15\n"
                   "jne 9f\n"
                   "xorl %%eax, %%eax\n"

                   "9:\n"
                   "popq %%rdi\n"
                   "popq %%r15\n"
                   "popq %%r14\n"
                   "popq %%r13\n"
                   "popq %%r12\n"
                   "popq %%rbp\n"
                   "popq %%rbx\n"
                   "ret\n" ::);
}

#endif
