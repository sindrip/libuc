#ifndef LIBUC_SRC_SYS_AUXV_AUXV_H
#define LIBUC_SRC_SYS_AUXV_AUXV_H

#include <stdint.h>

void __libuc_auxv_init(const uintptr_t *auxv);
[[nodiscard]] bool __libuc_auxv_get(uintptr_t type, uintptr_t *value);

#endif
