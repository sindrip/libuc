#include "auxv.h"

#include <linux/auxvec.h>

#include "crash.h"

static size_t page_size;

void rt_auxv_init(void *stack) {
  unsigned long *p = stack;

  unsigned long argc = *p++;
  p += argc;
  p++;

  while (*p != 0) {
    p++;
  }
  p++;

  for (; p[0] != AT_NULL; p += 2) {
    if (p[0] == AT_PAGESZ) {
      page_size = (size_t)p[1];
    }
  }
}

size_t rt_page_size(void) {
  if (page_size == 0) {
    rt_panic("auxv: no AT_PAGESZ", __builtin_return_address(0));
  }

  if ((page_size & (page_size - 1)) != 0) {
    rt_panic("auxv: AT_PAGESZ is not a power of two",
             __builtin_return_address(0));
  }

  return page_size;
}
