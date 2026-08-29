/* Linux x86-64 process entry. The initial stack begins with argc, followed
 * by argv, envp, and auxv. No C runtime has executed yet: the frame-pointer
 * chain ends here, and the kernel-provided stack address travels to
 * __libuc_start as its argument before rsp is aligned -- the call then
 * leaves rsp congruent to 8 mod 16 at the callee, as the psABI requires.
 * __libuc_start returns main's status, which rides to exit_group; ud2
 * documents that exit_group cannot return. */

#include <asm/unistd.h>

void _start(void);

[[gnu::naked]] void _start(void) {
  __asm__ volatile("xorl %%ebp, %%ebp\n"
                   "movq %%rsp, %%rdi\n"
                   "andq $-16, %%rsp\n"
                   "callq __libuc_start\n"
                   "movl %%eax, %%edi\n"
                   "movl $%c[exit_group], %%eax\n"
                   "syscall\n"
                   "ud2\n"
                   :
                   : [exit_group] "i"(__NR_exit_group));
}
