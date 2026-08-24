#ifndef LIBUC_SYS_AUXV_H
#define LIBUC_SYS_AUXV_H

#include <linux/auxvec.h>

unsigned long getauxval(unsigned long type);

#endif
