/*
 * Aux vector parsing. Contracts are in auxv.h.
 *
 * Walked as pairs of unsigned long rather than through a struct: the kernel's
 * uapi headers do not define one (Elf64_auxv_t lives in libc's <elf.h>), and
 * inventing a struct here would be retyping an ABI the headers do not carry —
 * two words per entry is the whole of it, so there is nothing a type would
 * add beyond a name.
 */

#include "auxv.h"

#include <linux/auxvec.h>

#include "crash.h"

static size_t page_size;

void rt_auxv_init(void *stack) {
  unsigned long *p = stack;

  /* The layout start.S documents: argc, argc argv pointers, a NULL, envp, a
   * NULL, then auxv. Nothing here is a guess — it is the ELF process entry
   * contract, and getting the walk wrong lands in envp strings that look like
   * plausible auxv types. */
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

  /* Every alignment calculation downstream masks with page - 1, which is only
   * a page mask if the size is a power of two. The architecture guarantees it;
   * the check is against a value that arrived from outside this program. */
  if ((page_size & (page_size - 1)) != 0) {
    rt_panic("auxv: AT_PAGESZ is not a power of two",
             __builtin_return_address(0));
  }

  return page_size;
}
