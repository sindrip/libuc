/*
 * The saved register state, and the primitives that move between two of them.
 *
 * Architecture, not fiber. A scheduler holds an rt_ctx as well as a fiber
 * does, and the switch is the same instruction sequence whichever side calls
 * it — so this lives beside neither.
 *
 * The contract is here; every machine fact is in arch/<arch>/, both the
 * struct's layout and the code that fills it. This file does not change when
 * a second architecture arrives — the same rule syscall.h states against
 * arch/<arch>/syscall_arch.h.
 *
 * io.c is pointedly not a consumer: describing an operation needs no notion
 * of a machine context.
 */
#ifndef RT_CONTEXT_H
#define RT_CONTEXT_H

#include "arch/aarch64/context_arch.h"

/* Opaque by construction, not by convention: there are no register fields to
 * reach, so writing to one outside arch/ is a compile error rather than a
 * review comment. Complete rather than incomplete because struct rt_fiber and
 * struct rt_scheduler embed a context by value — an incomplete type cannot be
 * a member, so the storage has to be declared even where the layout is not.
 *
 * Free at runtime, measured rather than assumed: rt_ctx_init compiles to the
 * same 17 instructions as writing the fields directly would, with its local
 * of the real layout eliminated outright — no stack traffic, and no memcpy
 * libcall, which matters because this runtime links no memcpy. The two forms
 * differ only in instruction scheduling.
 *
 * What it costs is paid in source, not cycles: one extra type name, and a
 * register layout that now lives in a .c file where no include can reach it. */
struct rt_ctx {
  alignas(RT_CTX_ALIGN) unsigned char opaque[RT_CTX_SIZE];
};

/* Saves the callee-saved state into *from, restores *to, and returns as *to.
 *
 * Exactly two call sites exist in the runtime, one per side of the fiber
 * boundary: rt_fiber_suspend (fiber.c) and rt_scheduler_resume
 * (scheduler.c). A third would mean some path transfers control without
 * going through the request protocol. */
extern void rt_switch(struct rt_ctx *from, struct rt_ctx *to);

/* Build a context that has never run, such that the first rt_switch into it
 * begins executing fn(arg) on the given stack.
 *
 * The arch boundary is this function rather than the struct, because the
 * struct leaks through whoever fills it: setting gp[0] and gp[1] is knowing
 * they are x19 and x20, and setting lr is knowing control resumes through a
 * link register at all. x86-64 has no link register — it would push a return
 * address onto the stack instead — so a caller that filled these fields by
 * hand would be aarch64 code living outside arch/.
 *
 * stack_top is the exclusive top of the usable region; aligning it is this
 * function's business, since how far down and to what boundary is an ABI
 * question. The caller supplies memory and says nothing about registers. */
void rt_ctx_init(struct rt_ctx *ctx, void *stack_top, void (*fn)(void *),
                 void *arg);

#endif /* RT_CONTEXT_H */
