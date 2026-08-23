/*
 * The aux vector: what the kernel tells a process about the machine it woke
 * up on. Parsed once at entry, before anything that could depend on it.
 *
 * This is crt1's job, and start.S already hands rt_main the untouched stack
 * pointer for exactly this reason. AT_RANDOM (stack guard seed) and AT_HWCAP
 * (feature probing) join AT_PAGESZ here when something needs them.
 */
#ifndef RT_AUXV_H
#define RT_AUXV_H

#include <stddef.h>

/* Walk the aux vector and cache what the runtime needs. Takes the stack
 * pointer as the kernel left it — argc, argv, envp and auxv all live there,
 * and start.S passes it in x0 before aligning sp. */
void rt_auxv_init(void *stack);

/* The kernel's page size, never a constant.
 *
 * aarch64 supports 4K, 16K and 64K pages and the choice is one Kconfig line —
 * CONFIG_ARM64_4K_PAGES on the pinned build. A hardcoded 4096 is right until
 * that line moves, and then mprotect rejects a misaligned guard boundary,
 * fiber creation exits, and PID 1 dying reports "Attempted to kill init",
 * which says nothing whatsoever about page sizes. It is exactly the drift
 * AGENTS.md accepts for PREEMPT, except this one is a boot failure with a
 * misleading cause rather than a performance surprise.
 *
 * Panics rather than guessing if rt_auxv_init has not run or the kernel did
 * not supply AT_PAGESZ: a wrong page size corrupts stack geometry silently. */
[[nodiscard]] size_t rt_page_size(void);

#endif /* RT_AUXV_H */
