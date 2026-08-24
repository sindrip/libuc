#ifndef LIBUC_SRC_THREAD_LOCAL_THREAD_LOCAL_H
#define LIBUC_SRC_THREAD_LOCAL_THREAD_LOCAL_H

#include <stddef.h>

struct __libuc_thread_local_layout {
  const unsigned char *image;
  size_t image_size;
  size_t block_size;
  size_t alignment;
};

[[nodiscard]] const struct __libuc_thread_local_layout *
__libuc_thread_local_layout(void);

#endif
