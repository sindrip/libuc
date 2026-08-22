/*
 * Stackful context switch. Every task suspension and resumption passes
 * through rt_switch, and a single wrong line here corrupts state that
 * surfaces far away, often only under optimisation or floating-point load.
 * Register reference: .scratch/aarch64.md.
 *
 * The model is the kernel's own cpu_switch_to (entry.S:821), with two
 * deliberate differences:
 *   1. d8–d15 are saved. The kernel does not, because kernel code never
 *      uses FP/SIMD and it saves that state separately via
 *      fpsimd_thread_switch. A userspace coroutine switch *must* save them,
 *      or floating-point values silently corrupt across yields — the kind
 *      of bug that appears months later and cannot be bisected.
 *   2. Fixed offsets instead of post-index addressing. The kernel's
 *      post-index `stp x19, x20, [x8], #16` is compact, but the offsets are
 *      implicit; explicit offsets match struct rt_ctx field by field, so
 *      this file and task.h can be verified side by side without
 *      arithmetic.
 *
 * The layout (task.h asserts the total):
 *
 *   offset  field
 *   ------  -----
 *      0    x19–x28       (five stp pairs)
 *     80    x29 (FP), x30 (LR)
 *     96    sp            (via scratch — sp cannot be an stp operand)
 *    104    d8–d15        (four stp pairs)
 *   ------
 *    168    total
 */

	.text
	.globl rt_switch
	.type  rt_switch, %function

/* void rt_switch(struct rt_ctx *from, struct rt_ctx *to);
 *   x0 = the context being left, x1 = the context being entered.
 *
 * From C's perspective this is an ordinary function call, so the caller has
 * already preserved everything caller-saved (x0–x18, d0–d7, d16–d31); only
 * the callee-saved set crosses the switch.
 *
 * The magic is `ret` at the end: it jumps to the restored x30, which for a
 * resumed task is wherever *it* last called rt_switch — the task resumes as
 * if rt_switch had returned normally. For a brand-new task, x30 was primed
 * to rt_task_entry (task.c), so `ret` enters the trampoline instead.
 */
rt_switch:
	stp  x19, x20, [x0, #0]
	stp  x21, x22, [x0, #16]
	stp  x23, x24, [x0, #32]
	stp  x25, x26, [x0, #48]
	stp  x27, x28, [x0, #64]
	stp  x29, x30, [x0, #80]

    /* sp cannot appear as a register operand in stp/str, so it goes through
     * a scratch — the kernel does the same. x2 is caller-saved and not an
     * argument (x0/x1 are still live). */
	mov  x2, sp
	str  x2, [x0, #96]


    /* Only the low 64 bits of v8–v15 are callee-saved (AAPCS64 §6.1.2), so
     * `d` registers, not `q`. Saving q would be wrong-but-harmless; saving
     * nothing is wrong-and-silent. */
	stp  d8,  d9,  [x0, #104]
	stp  d10, d11, [x0, #120]
	stp  d12, d13, [x0, #136]
	stp  d14, d15, [x0, #152]

    /* ---- the boundary: everything above wrote "from", below reads "to" ---- */

    /* Mirror image at the same offsets: every save line has a matching
     * restore line, verifiable by eye. */
	ldp  x19, x20, [x1, #0]
	ldp  x21, x22, [x1, #16]
	ldp  x23, x24, [x1, #32]
	ldp  x25, x26, [x1, #48]
	ldp  x27, x28, [x1, #64]
	ldp  x29, x30, [x1, #80]
	ldr  x2,       [x1, #96]
	mov  sp, x2
	ldp  d8,  d9,  [x1, #104]
	ldp  d10, d11, [x1, #120]
	ldp  d12, d13, [x1, #136]
	ldp  d14, d15, [x1, #152]


	ret


	.size rt_switch, . - rt_switch

/*
 * First entry into a fresh task. It has never called rt_switch, so there is
 * no natural x30 to return into; rt_task_create primes the context instead:
 *   x19 = the task's function pointer
 *   x20 = the task's argument
 *   x30 = rt_task_entry
 * so rt_switch's `ret` lands on the first instruction here.
 */
	.globl rt_task_entry
	.type  rt_task_entry, %function
rt_task_entry:
    /* Push a root frame record with both halves null: a null saved-fp
     * terminates the crash handler's walk, exactly as _start zeroes x29,
     * and there is no real return address to record. stp with pre-index
     * `[sp, #-16]!` is the canonical push. */
	stp xzr, xzr, [sp, #-16]!
	mov x29, sp

	mov x0, x20
	blr x19


    /* The task function returned. No `ret` — there is no valid return
     * address below — so a plain branch, never coming back: rt_task_exit
     * marks the task dead and switches away for good. */
	b rt_task_exit


	.size rt_task_entry, . - rt_task_entry

	.section .note.GNU-stack, "", %progbits
