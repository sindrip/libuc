#ifndef LIBUC_SRC_ARCH_AARCH64_FIBER_ARCH_H
#define LIBUC_SRC_ARCH_AARCH64_FIBER_ARCH_H

struct fiber_arch_context {
  unsigned long gp[10]; /* x19..x28 */
  unsigned long fp;     /* x29 */
  unsigned long lr;     /* x30 */
  unsigned long sp;
  unsigned long d[8]; /* d8..d15 */
};

#endif
