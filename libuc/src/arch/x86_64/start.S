/* Linux x86-64 process entry. The initial stack begins with argc, followed
 * by argv, envp, and auxv. No C runtime has executed yet. */
#include <asm/unistd.h>

	.text
	.p2align 4
	.globl _start
	.type _start, @function
_start:
	/* End the frame-pointer chain before the first C frame is created. */
	xor %ebp, %ebp

	/* Preserve the kernel-provided stack address as the startup argument.
	 * The call below then leaves rsp congruent to 8 mod 16 at the callee,
	 * as the ABI requires. */
	mov %rsp, %rdi
	and $-16, %rsp

	call __libuc_start

	/* __libuc_start returns main's status in %eax. */
	mov %eax, %edi
	mov $__NR_exit_group, %eax
	syscall

	/* exit_group cannot return. */
	ud2

	.size _start, . - _start
	.section .note.GNU-stack, "", @progbits
