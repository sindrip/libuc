/* Linux AArch64 process entry. The initial stack begins with argc, followed
 * by argv, envp, and auxv. No C runtime has executed yet: the frame-pointer
 * chain ends here, and the kernel-provided stack address travels to
 * __libuc_start as its argument before sp is aligned for the call. */

#include <asm/unistd.h>

void _start(void);

[[gnu::naked]] void _start(void) {
  __asm__ volatile("mov x29, xzr\n"
                   "mov x30, xzr\n"
                   "mov x0, sp\n"
                   "and sp, x0, #-16\n"
                   "bl __libuc_start\n"
                   "mov x8, #%c[exit_group]\n"
                   "svc #0\n"
                   "brk #0\n"
                   :
                   : [exit_group] "i"(__NR_exit_group));
}
