#include <stddef.h>
#include <stdint.h>

#include "context.h"

struct rt_ctx_regs {
  unsigned long gp[10];
  unsigned long fp;
  unsigned long lr;
  unsigned long sp;
  double d[8];
};

static_assert(sizeof(struct rt_ctx_regs) == RT_CTX_SIZE);
static_assert(alignof(struct rt_ctx_regs) == RT_CTX_ALIGN);

static constexpr size_t ctx_gp = offsetof(struct rt_ctx_regs, gp);
static constexpr size_t ctx_fp = offsetof(struct rt_ctx_regs, fp);
static constexpr size_t ctx_sp = offsetof(struct rt_ctx_regs, sp);
static constexpr size_t ctx_d = offsetof(struct rt_ctx_regs, d);

static_assert(offsetof(struct rt_ctx_regs, lr) ==
              ctx_fp + sizeof(unsigned long));

#define I(...) " " #__VA_ARGS__ "\n"

[[gnu::naked]] void rt_switch(struct rt_ctx *from, struct rt_ctx *to) {
  __asm__ volatile(
      I(stp x19, x20, [x0, #%c[ctx_gp]])
      I(stp x21, x22, [x0, #(%c[ctx_gp] + 16)])
      I(stp x23, x24, [x0, #(%c[ctx_gp] + 32)])
      I(stp x25, x26, [x0, #(%c[ctx_gp] + 48)])
      I(stp x27, x28, [x0, #(%c[ctx_gp] + 64)])
      I(stp x29, x30, [x0, #%c[ctx_fp]])

      I(mov x2, sp)
      I(str x2, [x0, #%c[ctx_sp]])

      I(stp d8,  d9,  [x0, #%c[ctx_d]])
      I(stp d10, d11, [x0, #(%c[ctx_d] + 16)])
      I(stp d12, d13, [x0, #(%c[ctx_d] + 32)])
      I(stp d14, d15, [x0, #(%c[ctx_d] + 48)])

      I(ldp x19, x20, [x1, #%c[ctx_gp]])
      I(ldp x21, x22, [x1, #(%c[ctx_gp] + 16)])
      I(ldp x23, x24, [x1, #(%c[ctx_gp] + 32)])
      I(ldp x25, x26, [x1, #(%c[ctx_gp] + 48)])
      I(ldp x27, x28, [x1, #(%c[ctx_gp] + 64)])
      I(ldp x29, x30, [x1, #%c[ctx_fp]])
      I(ldr x2, [x1, #%c[ctx_sp]])
      I(mov sp, x2)
      I(ldp d8,  d9,  [x1, #%c[ctx_d]])
      I(ldp d10, d11, [x1, #(%c[ctx_d] + 16)])
      I(ldp d12, d13, [x1, #(%c[ctx_d] + 32)])
      I(ldp d14, d15, [x1, #(%c[ctx_d] + 48)])

      I(ret)
      :
      : [ctx_gp] "i"(ctx_gp), [ctx_fp] "i"(ctx_fp), [ctx_sp] "i"(ctx_sp),
        [ctx_d] "i"(ctx_d));
}

[[gnu::naked]] static void rt_fiber_entry(void) {
  __asm__ volatile(

      I(stp xzr, xzr, [sp, #-16]!)
      I(mov x29, sp)

      I(mov x0, x20)
      I(blr x19)

      I(b rt_fiber_exit));
}

#undef I

void rt_ctx_init(struct rt_ctx *ctx, void *stack_top, void (*fn)(void *),
                 void *arg) {

  struct rt_ctx_regs regs = {};

  regs.fp = 0;

  regs.sp = (unsigned long)(uintptr_t)stack_top & ~(unsigned long)15;
  regs.lr = (unsigned long)(uintptr_t)rt_fiber_entry;
  regs.gp[0] = (unsigned long)(uintptr_t)fn;
  regs.gp[1] = (unsigned long)(uintptr_t)arg;

  __builtin_memcpy(ctx, &regs, sizeof regs);
}
