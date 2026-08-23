#include <stddef.h>
#include <stdint.h>

#include "context.h"

static_assert(offsetof(struct rt_ctx_regs, lr) ==
              offsetof(struct rt_ctx_regs, fp) + sizeof(unsigned long));
static_assert(offsetof(struct rt_ctx_regs, gp[8]) ==
              offsetof(struct rt_ctx_regs, gp) + 64);
static_assert(offsetof(struct rt_ctx_regs, d[6]) ==
              offsetof(struct rt_ctx_regs, d) + 48);

#define I(...) " " #__VA_ARGS__ "\n"

#define P(op, a, b, base, sym, off)                                            \
  " " #op " " #a ", " #b ", [" #base ", #(%c[" #sym "] + " #off ")]\n"

#define GPRS(op, base)                                                         \
  P(op, x19, x20, base, ctx_gp, 0)                                             \
  P(op, x21, x22, base, ctx_gp, 16)                                            \
  P(op, x23, x24, base, ctx_gp, 32)                                            \
  P(op, x25, x26, base, ctx_gp, 48)                                            \
  P(op, x27, x28, base, ctx_gp, 64)                                            \
  P(op, x29, x30, base, ctx_fp, 0)

#define FPRS(op, base)                                                         \
  P(op, d8, d9, base, ctx_d, 0)                                                \
  P(op, d10, d11, base, ctx_d, 16)                                             \
  P(op, d12, d13, base, ctx_d, 32)                                             \
  P(op, d14, d15, base, ctx_d, 48)

[[gnu::naked]] void rt_switch(struct rt_ctx *from, struct rt_ctx *to) {
  __asm__ volatile(
      GPRS(stp, x0)
      I(mov x2, sp)
      I(str x2, [x0, #%c[ctx_sp]])
      FPRS(stp, x0)

      GPRS(ldp, x1)
      I(ldr x2, [x1, #%c[ctx_sp]])
      I(mov sp, x2)
      FPRS(ldp, x1)

      I(ret)
      :
      : [ctx_gp] "i"(offsetof(struct rt_ctx_regs, gp)),
        [ctx_fp] "i"(offsetof(struct rt_ctx_regs, fp)),
        [ctx_sp] "i"(offsetof(struct rt_ctx_regs, sp)),
        [ctx_d] "i"(offsetof(struct rt_ctx_regs, d)));
}

[[gnu::naked]] static void rt_fiber_entry(void) {
  __asm__ volatile(

      I(stp xzr, xzr, [sp, #-16]!)
      I(mov x29, sp)

      I(mov x0, x20)
      I(blr x19)

      I(b rt_fiber_exit));
}

#undef FPRS
#undef GPRS
#undef P
#undef I

void rt_ctx_init(struct rt_ctx *ctx, void *stack_top, void (*fn)(void *),
                 void *arg) {

  struct rt_ctx_regs regs = {};

  regs.sp = (unsigned long)(uintptr_t)stack_top & ~(unsigned long)15;
  regs.lr = (unsigned long)(uintptr_t)rt_fiber_entry;
  regs.gp[0] = (unsigned long)(uintptr_t)fn;
  regs.gp[1] = (unsigned long)(uintptr_t)arg;

  __builtin_memcpy(ctx, &regs, sizeof regs);
}
