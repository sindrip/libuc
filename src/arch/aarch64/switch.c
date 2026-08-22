/*
 * Stackful context switch. Every task suspension and resumption passes
 * through rt_switch, and a single wrong line here corrupts state that
 * surfaces far away, often only under optimisation or floating-point load.
 * Register reference: .scratch/aarch64.md.
 *
 * Naked functions, not a .S file: the bodies are pure asm — Clang emits no
 * prologue, epilogue, or instrumentation around them — and the layout comes
 * from struct rt_ctx via offsetof rather than a hand-kept offset table. The
 * struct is the single authority: fields can move and the asm follows.
 *
 * One offset, one name, three stages: offsetof extracts it, constexpr names
 * it in C (where the adjacency assert below also spends it), and an "i"
 * operand carries it across into the instruction text. Immediate operands
 * are what make extended asm sound in a naked function — they need no setup
 * code, so the absent prologue has nothing to omit. Every crossing is
 * spelled %c[...] at the point of use rather than aliased to an assembler
 * symbol: the sigil is where the C/asm boundary actually is, and hiding it
 * would read as if the asm dereferenced the C object.
 *
 * The model is the kernel's own cpu_switch_to (entry.S:821), with two
 * deliberate differences:
 *   1. d8–d15 are saved. The kernel does not, because kernel code never
 *      uses FP/SIMD and it saves that state separately via
 *      fpsimd_thread_switch. A userspace coroutine switch *must* save them,
 *      or floating-point values silently corrupt across yields — the kind
 *      of bug that appears months later and cannot be bisected.
 *   2. The kernel reaches cpu_context through a generated offset
 *      (asm-offsets.c:44, consumed at entry.S:823) and walks it with
 *      implicit post-index addressing; here the compiler computes every
 *      offset directly from the struct — the same single-authority idea,
 *      without the generation step.
 */

#include <stddef.h>

#include "../../task.h"

/* The layout, named once: consumed by the assert below and by the asm
 * operand lists. Never address-taken, so no storage is emitted. */
static constexpr size_t ctx_gp = offsetof(struct rt_ctx, gp);
static constexpr size_t ctx_fp = offsetof(struct rt_ctx, fp);
static constexpr size_t ctx_sp = offsetof(struct rt_ctx, sp);
static constexpr size_t ctx_d = offsetof(struct rt_ctx, d);

/* stp stores its pair contiguously. The gp and d pairs are stepped inside
 * arrays, contiguous by definition; x29/x30 is the one pair spanning two
 * distinct members, so its adjacency is the single layout fact the offsets
 * cannot express on their own. */
static_assert(offsetof(struct rt_ctx, lr) == ctx_fp + sizeof(unsigned long));

/* One line of asm text: stringize the tokens, own the separator. The quotes
 * and the \n stop being per-line clutter, so a body line reads as the
 * instruction it is. Variadic because the operands contain commas. */
#define I(...) " " #__VA_ARGS__ "\n"

/* From C's perspective an ordinary call, so the caller has already
 * preserved everything caller-saved (x0-x18, d0-d7, d16-d31); only the
 * callee-saved set crosses the switch. The parameters are never referenced
 * by name — a naked body may contain only asm — but the ABI has already
 * placed them in x0 and x1, which is what the body assumes.
 *
 * The magic is the final `ret`: it jumps to the restored x30, which for a
 * resumed task is wherever *it* last called rt_switch — the task resumes as
 * if rt_switch had returned normally. For a brand-new task, x30 was primed
 * to rt_task_entry (task.c), so `ret` enters the trampoline instead.
 */
[[gnu::naked]] void rt_switch(struct rt_ctx *from, struct rt_ctx *to) {
  __asm__ volatile(
      I(stp x19, x20, [x0, #%c[ctx_gp]])
      I(stp x21, x22, [x0, #(%c[ctx_gp] + 16)])
      I(stp x23, x24, [x0, #(%c[ctx_gp] + 32)])
      I(stp x25, x26, [x0, #(%c[ctx_gp] + 48)])
      I(stp x27, x28, [x0, #(%c[ctx_gp] + 64)])
      I(stp x29, x30, [x0, #%c[ctx_fp]])

      /* sp cannot appear as an stp/str operand, so it goes through a
       * scratch — the kernel does the same. x2 is caller-saved and not an
       * argument (x0/x1 are still live). */
      I(mov x2, sp)
      I(str x2, [x0, #%c[ctx_sp]])

      /* Only the low 64 bits of v8-v15 are callee-saved (AAPCS64 §6.1.2),
       * so `d` registers, not `q`. Saving q would be wrong-but-harmless;
       * saving nothing is wrong-and-silent. */
      I(stp d8,  d9,  [x0, #%c[ctx_d]])
      I(stp d10, d11, [x0, #(%c[ctx_d] + 16)])
      I(stp d12, d13, [x0, #(%c[ctx_d] + 32)])
      I(stp d14, d15, [x0, #(%c[ctx_d] + 48)])

      /* ---- the boundary: everything above wrote *from, below reads *to ---- */

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

/* First entry into a fresh task. It has never called rt_switch, so there is
 * no natural x30 to return into; rt_task_create primes the context instead:
 *   x19 = the task's function pointer
 *   x20 = the task's argument
 *   x30 = rt_task_entry
 * so rt_switch's `ret` lands on the first instruction here.
 */
[[gnu::naked]] void rt_task_entry(void) {
  __asm__ volatile(
      /* Push a root frame record with both halves null: a null saved-fp
       * terminates the crash handler's walk, exactly as _start zeroes x29,
       * and there is no real return address to record. stp with pre-index
       * `[sp, #-16]!` is the canonical push. */
      I(stp xzr, xzr, [sp, #-16]!)
      I(mov x29, sp)

      I(mov x0, x20)
      I(blr x19)

      /* The task function returned. No `ret` — there is no valid return
       * address below — so a plain branch, never coming back: rt_task_exit
       * marks the task dead and switches away for good. */
      I(b rt_task_exit));
}

#undef I
