/*
 * The aarch64 machine context: which registers must survive a switch.
 *
 * Name and place follow syscall_arch.h, and for the same reason — architecture
 * is a path, not a suffix. What is here is AAPCS64's callee-saved set and
 * nothing more general: x86-64 would be six GPRs (rbx, rbp, r12-r15) plus rsp
 * and no floating-point registers at all, since SysV saves none.
 *
 * Not opaque. Both struct rt_fiber and struct rt_scheduler embed one by value,
 * so hiding it would mean a sized byte array plus a paired internal type — the
 * jmp_buf idiom, which exists because jmp_buf sits in a public ABI where its
 * size is a promise. Nothing outside arch/ writes these fields anyway;
 * rt_ctx_init (context.h) is the only writer, which is checkable by grep.
 */
#ifndef RT_CONTEXT_ARCH_H
#define RT_CONTEXT_ARCH_H

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

#endif /* RT_CONTEXT_ARCH_H */
