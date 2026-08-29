#ifndef LIBUC_SRC_ARCH_AARCH64_FIBER_ARCH_H
#define LIBUC_SRC_ARCH_AARCH64_FIBER_ARCH_H

#include <stddef.h>

struct fiber_arch_context {
  unsigned long gp[10]; /* x19..x28 */
  unsigned long fp;     /* x29 */
  unsigned long lr;     /* x30 */
  unsigned long sp;
  unsigned long d[8]; /* d8..d15 */
};

[[gnu::naked]] [[maybe_unused]] static void
fiber_arch_switch([[maybe_unused]] struct fiber_arch_context *save,
                  [[maybe_unused]] const struct fiber_arch_context *restore) {
  __asm__ volatile("stp x19, x20, [x0, #%c[gp]]\n"
                   "stp x21, x22, [x0, #(%c[gp] + 16)]\n"
                   "stp x23, x24, [x0, #(%c[gp] + 32)]\n"
                   "stp x25, x26, [x0, #(%c[gp] + 48)]\n"
                   "stp x27, x28, [x0, #(%c[gp] + 64)]\n"
                   "stp x29, x30, [x0, #%c[fp]]\n"
                   "mov x2, sp\n"
                   "str x2, [x0, #%c[sp]]\n"
                   "stp d8, d9, [x0, #%c[d]]\n"
                   "stp d10, d11, [x0, #(%c[d] + 16)]\n"
                   "stp d12, d13, [x0, #(%c[d] + 32)]\n"
                   "stp d14, d15, [x0, #(%c[d] + 48)]\n"

                   "ldp x19, x20, [x1, #%c[gp]]\n"
                   "ldp x21, x22, [x1, #(%c[gp] + 16)]\n"
                   "ldp x23, x24, [x1, #(%c[gp] + 32)]\n"
                   "ldp x25, x26, [x1, #(%c[gp] + 48)]\n"
                   "ldp x27, x28, [x1, #(%c[gp] + 64)]\n"
                   "ldp x29, x30, [x1, #%c[fp]]\n"
                   "ldr x2, [x1, #%c[sp]]\n"
                   "mov sp, x2\n"
                   "ldp d8, d9, [x1, #%c[d]]\n"
                   "ldp d10, d11, [x1, #(%c[d] + 16)]\n"
                   "ldp d12, d13, [x1, #(%c[d] + 32)]\n"
                   "ldp d14, d15, [x1, #(%c[d] + 48)]\n"
                   "ret\n"
                   :
                   : [gp] "i"(offsetof(struct fiber_arch_context, gp)),
                     [fp] "i"(offsetof(struct fiber_arch_context, fp)),
                     [sp] "i"(offsetof(struct fiber_arch_context, sp)),
                     [d] "i"(offsetof(struct fiber_arch_context, d)));
}

#endif
