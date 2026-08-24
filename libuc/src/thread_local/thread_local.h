#ifndef LIBUC_SRC_THREAD_LOCAL_THREAD_LOCAL_H
#define LIBUC_SRC_THREAD_LOCAL_THREAD_LOCAL_H

#include <stddef.h>

struct __libuc_thread_local_image {
  const unsigned char *initialization;
  size_t initialized_size;
  size_t size;
  size_t alignment;
};

[[nodiscard]] bool __libuc_thread_local_image_init(void);
[[nodiscard]] const struct __libuc_thread_local_image *
__libuc_thread_local_image_get(void);

#endif
