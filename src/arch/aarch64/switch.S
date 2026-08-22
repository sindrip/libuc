/*
 * Stackful context switch for aarch64.
 *
 * This is arguably the most important file in the runtime: every task
 * suspension and resumption passes through rt_switch. It is also the most
 * subtle — a single wrong constraint corrupts state in a way that surfaces
 * far from here, often only under optimisation or floating-point load.
 *
 * Read .scratch/aarch64.md first. The register table there explains *what*
 * is callee-saved; this file is *how* we save and restore it.
 *
 * The model is the kernel's own cpu_switch_to (entry.S:821), with two
 * differences:
 *   1. We save d8–d15. The kernel does not, because kernel code never uses
 *      FP/SIMD and it saves that state separately via fpsimd_thread_switch.
 *      A userspace coroutine switch *must* save them, or floating-point
 *      values silently corrupt across yields — the kind of bug that does
 *      not appear until months later and is impossible to bisect.
 *   2. We use fixed offsets instead of post-index addressing. The kernel's
 *      post-index `stp x19, x20, [x8], #16` is compact, but the offsets
 *      are implicit — you have to count pairs to know where sp ended up.
 *      Explicit offsets match the C struct field-by-field, so you can read
 *      switch.S and task.h side by side and verify them without arithmetic.
 *
 * The struct layout (from task.h):
 *
 *   offset  field
 *   ------  -----
 *      0    x19–x20       (stp pair)
 *     16    x21–x22
 *     32    x23–x24
 *     48    x25–x26
 *     64    x27–x28
 *     80    x29 (FP), x30 (LR)
 *     96    sp            (via scratch register — see the sp save below)
 *    104    d8–d9         (stp pair, 8 bytes each)
 *    120    d10–d11
 *    136    d12–d13
 *    152    d14–d15
 *   ------
 *    168    total (round to 176 for 16-byte alignment)
 */

	.text
	.globl rt_switch
	.type  rt_switch, %function

/* void rt_switch(struct rt_ctx *from, struct rt_ctx *to);
 *   x0 = pointer to the context we are leaving
 *   x1 = pointer to the context we are entering
 *
 * From C's perspective this is an ordinary function call, so the caller has
 * already saved everything it cares about in the caller-saved set (x0–x18,
 * d0–d7, d16–d31). We save only the callee-saved set.
 *
 * The magic is `ret` at the end: it jumps to the restored x30, which for a
 * resumed task is wherever *it* last called rt_switch. For a brand-new task,
 * x30 was primed to rt_task_entry (see task.c), so `ret` enters the
 * trampoline instead.
 */
rt_switch:
    /* Save general-purpose callee-saved registers.
     *
     * Use `stp` (store pair) with explicit offsets into `from` (x0),
     * matching the struct layout above. Five pairs:
     *   x19/x20 at offset 0
     *   x21/x22 at offset 16
     *   x23/x24 at offset 32
     *   x25/x26 at offset 48
     *   x27/x28 at offset 64
     * Then x29/x30 together at offset 80.
     *
     * stp stores two 64-bit registers in one instruction. The offset is
     * in bytes and must be a multiple of 8 for 64-bit stores.
     */
	stp  x19, x20, [x0, #0]
	stp  x21, x22, [x0, #16]
	stp  x23, x24, [x0, #32]
	stp  x25, x26, [x0, #48]
	stp  x27, x28, [x0, #64]
	stp  x29, x30, [x0, #80]

    /* Save sp.
     *
     * `stp x29, sp, [x0, #80]` does NOT work — sp cannot appear as a
     * register operand in stp (see .scratch/aarch64.md, "sp is not a
     * general register"). The kernel solves this the same way: copy sp
     * to a scratch register first, then str the scratch register.
     *
     * Use x2, which is caller-saved and not one of our arguments (x0/x1
     * are still live). Store at offset 96.
     */
	mov  x2, sp
	str  x2, [x0, #96]


    /* Save floating-point callee-saved registers.
     *
     * d8–d15. Only the low 64 bits of v8–v15 are callee-saved (AAPCS64
     * §6.1.2), so we use `d` (64-bit) not `q` (128-bit). Saving `q`
     * would be wrong-but-harmless (more bytes, still covers the required
     * part). Saving nothing is wrong-and-silent.
     *
     * Each `d` register is 8 bytes. stp for `d` registers stores 16 bytes
     * per pair, same as for `x` registers. Four pairs at offsets 104, 120,
     * 136, 152.
     */
	stp  d8,  d9,  [x0, #104]
	stp  d10, d11, [x0, #120]
	stp  d12, d13, [x0, #136]
	stp  d14, d15, [x0, #152]

    /* ---- the boundary: everything above wrote "from", below reads "to" ---- */

    /* Restore everything from the "to" context.
     *
     * Mirror image of the save, reading from x1 instead of writing to x0.
     * Order does not technically matter, but keeping it the same makes
     * verification trivial: every save line has a matching restore line at
     * the same offset.
     *
     * Use ldp/ldr to restore: general-purpose pairs, then sp (via scratch),
     * then floating-point pairs. Restore sp the same way you saved it —
     * ldr into a scratch, then mov sp from the scratch.
     */
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


    /* Return into the other context.
     *
     * `ret` jumps to x30 — which we just loaded from "to". If "to" was
     * previously suspended inside rt_switch, x30 points to its caller's
     * return address, and execution resumes there as if rt_switch had
     * returned normally.
     *
     * If "to" is a brand-new task, x30 was primed to rt_task_entry, so
     * `ret` enters the trampoline. See task.c.
     */
	ret


	.size rt_switch, . - rt_switch

/*
 * Task entry trampoline.
 *
 * A freshly spawned task has never called rt_switch, so there is no natural
 * x30 to return into. Instead, rt_task_create primes the new context:
 *   x19 = the task's function pointer
 *   x20 = the task's argument
 *   x30 = rt_task_entry (this function)
 *
 * When rt_switch does `ret` into a new task for the first time, it lands
 * here.
 */
	.globl rt_task_entry
	.type  rt_task_entry, %function
rt_task_entry:
    /* Establish a root frame.
     *
     * Push a frame record with a null saved-FP. This terminates the FP
     * chain for RT-007's crash handler, just like _start zeroes x29.
     * Without this, the crash handler would walk off this stack into
     * garbage.
     *
     * A frame record is [saved_x29, saved_x30] pushed as a pair. Since
     * this is the root frame, both are zero — null FP terminates the
     * chain, and there is no real return address.
     *
     * stp with pre-index `[sp, #-16]!` is the canonical push on aarch64:
     * it decrements sp by 16 first, then stores the pair at the new sp.
     * Then set x29 = sp so the frame pointer points at the record.
     */
	stp xzr, xzr, [sp, #-16]!
	mov x29, sp

    /* Call the task function.
     *
     * x19 = fn, x20 = arg (primed by rt_task_create, survived the switch
     * because they are callee-saved). Move arg to x0 (first argument
     * register) and branch-with-link to fn.
     *
     * Use `blr` (branch with link to register) so that if the task
     * function returns, execution continues at the next instruction.
     */
	mov x0, x20
	blr x19


    /* The task function returned — hand off to C.
     *
     * We must NOT `ret` here: there is no valid return address, and the
     * stack has nothing useful below. Instead, branch to rt_task_exit
     * (in task.c), which marks the task dead and switches to the
     * scheduler. rt_task_exit never returns.
     *
     * Use `b` (plain branch, no link) since we never come back.
     */
	b rt_task_exit


	.size rt_task_entry, . - rt_task_entry

	.section .note.GNU-stack, "", %progbits
