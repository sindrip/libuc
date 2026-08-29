#ifndef RT_AUXV_H
#define RT_AUXV_H

#include <stddef.h>

void rt_auxv_init(void *stack);

[[nodiscard]] size_t rt_page_size(void);

#endif
