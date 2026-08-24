#include "auxv.h"

#include <sys/auxv.h>

static const uintptr_t *auxiliary_vector;

void __libuc_auxv_init(const uintptr_t *auxv) { auxiliary_vector = auxv; }

bool __libuc_auxv_get(uintptr_t type, uintptr_t *value) {
  for (const uintptr_t *entry = auxiliary_vector; entry[0] != AT_NULL;
       entry += 2) {
    if (entry[0] == type) {
      *value = entry[1];
      return true;
    }
  }

  return false;
}

unsigned long getauxval(unsigned long type) {
  (void)type;
  __builtin_trap();
}
