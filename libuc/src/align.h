#ifndef LIBUC_SRC_ALIGN_H
#define LIBUC_SRC_ALIGN_H

#include <stddef.h>

/* Every alignment is a power of two, which callers validate where the value
 * enters rather than on each use.
 *
 * Rounding up is unchecked: raising by alignment - 1 wraps when value comes
 * within that of SIZE_MAX, and the result is then a small multiple rather than
 * a failure. Nothing reaches that range today — every value is a layout the
 * linker already sized — so the check is deferred. */
[[nodiscard]] static inline size_t __libuc_align_up(size_t value,
                                                    size_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

[[nodiscard]] static inline size_t __libuc_align_down(size_t value,
                                                      size_t alignment) {
  return value & ~(alignment - 1);
}

#endif
