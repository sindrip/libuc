#include <stdint.h>

#include <errno.h>
#include <sys/auxv.h>

#include "auxv.h"

unsigned long getauxval(unsigned long type) {
  uintptr_t value;
  if (!__libuc_auxv_get(type, &value)) {
    errno = ENOENT;
    return 0;
  }

  return value;
}
