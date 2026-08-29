#ifndef LIBUC_SRC_THREAD_LOCAL_THREAD_LOCAL_H
#define LIBUC_SRC_THREAD_LOCAL_THREAD_LOCAL_H

#include <stddef.h>

/* The executable's static thread-local image, as its PT_TLS segment declares
 * it. Every thread-local block begins life as image_size bytes copied from
 * image, then zeros up to block_size, placed at a multiple of alignment. */
struct __libuc_thread_local_layout {
  const unsigned char *image; /* p_filesz initialization bytes; null if none */
  size_t image_size;          /* p_filesz */
  size_t block_size;          /* p_memsz: the image plus its zero-filled tail */
  size_t alignment;           /* p_align, a power of two; 1 without PT_TLS */
};

[[nodiscard]] const struct __libuc_thread_local_layout *
__libuc_thread_local_layout(void);

#endif
