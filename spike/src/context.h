#ifndef RT_CONTEXT_H
#define RT_CONTEXT_H

#include "context_arch.h"

struct rt_ctx {
  alignas(struct rt_ctx_regs) unsigned char opaque[sizeof(struct rt_ctx_regs)];
};

extern void rt_switch(struct rt_ctx *from, struct rt_ctx *to);

void rt_ctx_init(struct rt_ctx *ctx, void *stack_top, void (*fn)(void *),
                 void *arg);

#endif
