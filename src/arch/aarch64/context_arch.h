/*
 * The size and alignment of an aarch64 machine context — and deliberately
 * nothing else.
 *
 * The layout itself is in switch.c, not in any header, so no translation unit
 * can reach the register fields even by including the wrong file. What the
 * rest of the runtime needs is exactly what it takes to embed one by value:
 * how big, and how aligned. That is the same bargain glibc strikes for
 * pthread_mutex_t and jmp_buf, and for the same reason — a caller that must
 * reserve storage has to know the size, and needs to know nothing more.
 *
 * These two numbers are checked against the real struct at compile time
 * (switch.c). Changing the register set without changing them fails the
 * build; changing them without cause fails it too.
 *
 * Name and place follow syscall_arch.h: architecture is a path, not a suffix.
 * x86-64's numbers would be smaller — six callee-saved GPRs plus rsp, and no
 * floating-point registers at all, since SysV preserves none.
 */
#ifndef RT_CONTEXT_ARCH_H
#define RT_CONTEXT_ARCH_H

#include <stddef.h>

/* x19-x28, x29, x30, sp: 13 doublewords. d8-d15: 8 more. */
constexpr size_t RT_CTX_SIZE = 168;

/* 8, not 16. The widest member is a doubleword, and stp/ldp of 64-bit
 * registers needs no more than that. 16 is the alignment sp must satisfy —
 * a different rule about a different object, and the reason this is easy to
 * get wrong is that both numbers show up two lines apart in switch.c. */
constexpr size_t RT_CTX_ALIGN = 8;

#endif /* RT_CONTEXT_ARCH_H */
