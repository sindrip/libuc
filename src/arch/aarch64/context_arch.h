#ifndef RT_CONTEXT_ARCH_H
#define RT_CONTEXT_ARCH_H

struct rt_ctx_regs {
  unsigned long gp[10];
  unsigned long fp;
  unsigned long lr;
  unsigned long sp;
  double d[8];
};

#endif
