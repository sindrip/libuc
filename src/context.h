#ifndef RT_CONTEXT_H
#define RT_CONTEXT_H

#include "arch/aarch64/context_arch.h"

struct rt_ctx {
  alignas(RT_CTX_ALIGN) unsigned char opaque[RT_CTX_SIZE];
};

extern void rt_switch(struct rt_ctx *from, struct rt_ctx *to);

void rt_ctx_init(struct rt_ctx *ctx, void *stack_top, void (*fn)(void *),
                 void *arg);

#endif
