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
 *   x0    may hold a function pointer to register with atexit, or 0. There is
 *	   no atexit here, so it is ignored — but do not assume it is zero.
 *   All other registers: undefined. Assume nothing.
 *
 * .S (capital) means this file is run through the C preprocessor, which is why
 * the #include below works and why syscall numbers are never hardcoded.
 */
#include <asm/unistd.h>

	.text
	.globl _start
	.type  _start, %function
_start:
    /* Terminate the frame-pointer chain: the crash handler walks
     * [x29] = saved x29, [x29+8] = saved x30 and stops at a null fp. If the
     * first frame inherited garbage from the kernel, the walk would chase it
     * into unmapped memory and fault while reporting a fault. */
	mov x29, xzr
	mov x30, xzr

    /* Pass the original stack pointer as rt_main's first argument:
     * argc/argv/envp/auxv all live at sp, and auxv (AT_PAGESZ, AT_HWCAP and
     * friends) will matter later. Captured before the alignment below
     * disturbs sp. */
	mov x0, sp

    /* Force 16-byte stack alignment. AAPCS64 requires it at every
     * instruction that uses sp; Linux hands over an aligned sp already, so
     * this is defensive — two instructions against a whole class of "works
     * until it doesn't". The immediate form of AND can target sp directly;
     * the shifted-register form cannot (.scratch/aarch64.md). */
	and sp, x0, #-16

	bl rt_main

    /* rt_main must never return. If it does, exit loudly: as PID 1 this
     * panics the kernel with "Attempted to kill init" — the intended
     * outcome, unmistakable rather than a silent hang. */
	mov x8, __NR_exit_group
	mov x0, 127
	svc #0

	.size _start, . - _start

/* Mark the stack non-executable. Without this the linker emits a warning and,
 * on some toolchains, an executable stack — never wanted here. */
	.section .note.GNU-stack, "", %progbits
