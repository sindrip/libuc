/* Linux AArch64 process entry. The initial stack begins with argc, followed
 * by argv, envp, and auxv. No C runtime has executed yet. */
#include <asm/unistd.h>

	.text
	.p2align 2
	.globl _start
	.type _start, %function
_start:
	/* End the frame-pointer chain before the first C frame is created. */
	mov x29, xzr
	mov x30, xzr

	/* Preserve the kernel-provided stack address as the startup argument. */
	mov x0, sp
	and sp, x0, #-16

	bl __libuc_start

	/* __libuc_start returns main's status in w0. */
	mov x8, __NR_exit_group
	svc #0

	/* exit_group cannot return. */
	brk #0

	.size _start, . - _start
	.section .note.GNU-stack, "", %progbits
