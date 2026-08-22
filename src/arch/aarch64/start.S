/*
 * Process entry. The kernel jumps here directly — there is no libc startup, no
 * crt1.o, nothing has run before this instruction.
 *
 * State on entry (Linux aarch64 ELF process entry):
 *
 *   sp -> [ argc		  ]   <- 16-byte aligned already, but see below
 *	   [ argv[0] ]
 *	   [ ...     ]
 *	   [ NULL    ]		       end of argv
 *	   [ envp[0] ]
 *	   [ ...     ]
 *	   [ NULL    ]		       end of envp
 *	   [ auxv... ]		       Elf64_auxv_t pairs, terminated by AT_NULL
 *
 *   x0    may hold a function pointer to register with atexit, or 0. We have no
 *	   atexit, so it is ignored — but do not assume it is zero.
 *   All other registers: undefined. Assume nothing.
 *
 * .S (capital) means this file is run through the C preprocessor, which is why
 * the #include below works and why we never hardcode a syscall number.
 */
#include <asm/unistd.h>

	.text
	.globl _start
	.type  _start, %function
_start:
    /* Terminate the frame-pointer chain.
     *
     * Zero x29 (frame pointer) and x30 (link register).
     *
     * This is not cosmetic. RT-007's crash handler walks the FP chain to
     * produce a backtrace: each frame is [x29] = saved x29, [x29+8] = saved
     * x30, and the walk stops when x29 is null. If the very first frame
     * inherits garbage from the kernel, the walk chases it into unmapped
     * memory and the crash handler faults while reporting a fault.
     *
     * The zero register on aarch64 is xzr.
     */
	mov x29, xzr
	mov x30, xzr

    /* Pass the original stack pointer as rt_main's first argument.
     *
     * argc/argv/envp/auxv all live at sp, and we will want auxv later (it
     * carries AT_PAGESZ, AT_HWCAP and friends). Capture it *before* touching
     * sp in the alignment step below.
     *
     * First argument goes in x0.
     */
	mov x0, sp

    /* Force 16-byte stack alignment.
     *
     * AAPCS64 requires sp to be 16-byte aligned at every instruction that uses
     * it. Linux hands us an aligned sp already, so this is defensive — but it
     * costs two instructions and removes a whole class of "works until it
     * doesn't" bug.
     *
     * Hint: the mask is -16, not ~15 — the assembler wants a plain immediate.
     * The immediate form of AND *can* target sp directly (verified), so no
     * scratch register is needed here. The shifted-register form cannot, which
     * is a separate rule worth knowing: see .scratch/aarch64.md.
     */
	and sp, x0, #-16

    /* Call rt_main. */
	bl rt_main

    /* rt_main must never return. If it does, exit loudly.
     *
     * exit_group(1) — syscall number in x8, status in x0, then svc #0.
     * Use __NR_exit_group, not a literal.
     *
     * Note what happens next if this ever runs: we are PID 1, so the kernel
     * panics with "Attempted to kill init". That is the intended outcome —
     * a loud, unmistakable failure rather than a silent hang.
     */
	mov x8, __NR_exit_group
	mov x0, 127
	svc #0

	.size _start, . - _start

/* Mark the stack non-executable. Without this the linker emits a warning and,
 * on some toolchains, an executable stack — which we never want. */
	.section .note.GNU-stack, "", %progbits
