#ifndef LIBUC_SRC_THREAD_LOCAL_THREAD_LOCAL_H
#define LIBUC_SRC_THREAD_LOCAL_THREAD_LOCAL_H

#include <stddef.h>

struct __libuc_fiber;

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

/* The runtime-owned storage for one execution context.  The thread pointer
 * is not installed by UC-004-min; that belongs to the next ticket. */
struct __libuc_thread_local_block {
  void *mapping;
  size_t mapping_length;
  unsigned char *block;
  void *thread_pointer;
};

struct __libuc_thread_local_tcb {
  struct __libuc_thread_local_tcb *self;
  struct __libuc_fiber *fiber;
};

[[nodiscard]] bool __libuc_thread_local_block_create(
    struct __libuc_thread_local_block *block);

[[nodiscard]] bool __libuc_thread_local_block_destroy(
    const struct __libuc_thread_local_block *block);

/* Whether user mode can write the thread register — each architecture
 * decides from the capability word. Ask at initialization, before the first
 * install. */
[[nodiscard]] bool __libuc_thread_local_install_available(void);

/* Make the block's recorded thread pointer current. Installs only: no
 * allocation, no copying, no ownership change. The caller has confirmed
 * availability; without it the write traps. */
void __libuc_thread_local_block_install(
    const struct __libuc_thread_local_block *block);

#endif
