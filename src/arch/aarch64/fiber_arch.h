#ifndef LIBUC_SRC_ARCH_AARCH64_FIBER_ARCH_H
#define LIBUC_SRC_ARCH_AARCH64_FIBER_ARCH_H

#include <stddef.h>
#include <stdint.h>

struct fiber_context {
  unsigned long gp[10]; /* x19..x28 */
  unsigned long fp;     /* x29 */
  unsigned long lr;     /* x30 */
  unsigned long sp;     /* sp */
  unsigned long d[8];   /* d8..d15 */
};

[[gnu::naked]] [[maybe_unused]] static unsigned long
fiber_switch(struct fiber_context *save, const struct fiber_context *restore,
             unsigned long token) {
  __asm__ volatile("stp x19, x20, [x0, #%c[x19]]\n"
                   "stp x21, x22, [x0, #%c[x21]]\n"
                   "stp x23, x24, [x0, #%c[x23]]\n"
                   "stp x25, x26, [x0, #%c[x25]]\n"
                   "stp x27, x28, [x0, #%c[x27]]\n"
                   "stp x29, x30, [x0, #%c[fp]]\n"
                   "mov x3, sp\n"
                   "str x3, [x0, #%c[sp]]\n"
                   "stp d8, d9, [x0, #%c[d8]]\n"
                   "stp d10, d11, [x0, #%c[d10]]\n"
                   "stp d12, d13, [x0, #%c[d12]]\n"
                   "stp d14, d15, [x0, #%c[d14]]\n"

                   "ldp x19, x20, [x1, #%c[x19]]\n"
                   "ldp x21, x22, [x1, #%c[x21]]\n"
                   "ldp x23, x24, [x1, #%c[x23]]\n"
                   "ldp x25, x26, [x1, #%c[x25]]\n"
                   "ldp x27, x28, [x1, #%c[x27]]\n"
                   "ldp x29, x30, [x1, #%c[fp]]\n"
                   "ldr x3, [x1, #%c[sp]]\n"
                   "mov sp, x3\n"
                   "ldp d8, d9, [x1, #%c[d8]]\n"
                   "ldp d10, d11, [x1, #%c[d10]]\n"
                   "ldp d12, d13, [x1, #%c[d12]]\n"
                   "ldp d14, d15, [x1, #%c[d14]]\n"

                   /* What the resumed side's own switch returns. */
                   "mov x0, x2\n"
                   "ret\n"
                   :
                   : [x19] "i"(offsetof(struct fiber_context, gp[0])),
                     [x21] "i"(offsetof(struct fiber_context, gp[2])),
                     [x23] "i"(offsetof(struct fiber_context, gp[4])),
                     [x25] "i"(offsetof(struct fiber_context, gp[6])),
                     [x27] "i"(offsetof(struct fiber_context, gp[8])),
                     [fp] "i"(offsetof(struct fiber_context, fp)),
                     [sp] "i"(offsetof(struct fiber_context, sp)),
                     [d8] "i"(offsetof(struct fiber_context, d[0])),
                     [d10] "i"(offsetof(struct fiber_context, d[2])),
                     [d12] "i"(offsetof(struct fiber_context, d[4])),
                     [d14] "i"(offsetof(struct fiber_context, d[6]))
                   : "memory");
}

/* A context built by fiber_context_make resumes here: x19 holds the
 * function, x20 its argument. The function must never return; x29 and x30
 * are zeroed so backtraces stop and a return faults. */
[[gnu::naked]] [[maybe_unused]] static void fiber_start(void) {
  __asm__ volatile("mov x29, xzr\n"
                   "mov x30, xzr\n"
                   "mov x0, x20\n"
                   "br x19\n");
}

static inline void fiber_context_make(struct fiber_context *context,
                                      unsigned char *stack_top,
                                      void (*function)(void *),
                                      void *argument) {
  *context = (struct fiber_context){
      .gp = {[0] = (unsigned long)(uintptr_t)function,
             [1] = (unsigned long)(uintptr_t)argument},
      .lr = (unsigned long)(uintptr_t)fiber_start,
      .sp = (unsigned long)(uintptr_t)stack_top & ~15UL,
  };
}

#endif
