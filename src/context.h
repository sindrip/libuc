/*
 * The saved register state, and the primitives that move between two of them.
 *
 * Architecture, not fiber. A scheduler holds an rt_ctx as well as a fiber
 * does, and the switch is the same instruction sequence whichever side calls
 * it — so this lives beside neither. Declared here, implemented per
 * architecture in arch/<arch>/switch.c, mirroring syscall.h against
 * arch/<arch>/syscall_arch.h.
 *
 * io.c is pointedly not a consumer: describing an operation needs no notion
 * of a machine context.
 */
#ifndef RT_CONTEXT_H
#define RT_CONTEXT_H

/* The single authority on the switch's layout: rt_switch
 * (arch/aarch64/switch.c) computes every store/load offset from these fields
 * via offsetof, so the struct is free to change shape and the asm follows —
 * there is no offset table to keep in sync. The one cross-member assumption
 * the offsets cannot express — fp and lr adjacent, for the x29/x30 stp pair —
 * is asserted in switch.c. */
struct rt_ctx {
  unsigned long gp[10]; /* x19-x28 */
  unsigned long fp;     /* x29 */
  unsigned long lr;     /* x30 */
  unsigned long sp;
  double d[8]; /* d8-d15 */
};

/* Saves the callee-saved state into *from, restores *to, and returns as *to.
 *
 * Exactly two call sites exist in the runtime, one per side of the fiber
 * boundary: rt_fiber_suspend (fiber.c) and rt_scheduler_resume
 * (scheduler.c). A third would mean some path transfers control without
 * going through the request protocol. */
extern void rt_switch(struct rt_ctx *from, struct rt_ctx *to);

/* First-entry trampoline (switch.c): calls fn(arg) out of x19/x20, then
 * branches to rt_fiber_exit. Named for the fiber but defined with the switch,
 * because it is the same piece of assembly seen from the other end — a fresh
 * context has no saved x30 to return into, so rt_fiber_create primes one
 * pointing here. */
extern void rt_fiber_entry(void);

#endif /* RT_CONTEXT_H */
