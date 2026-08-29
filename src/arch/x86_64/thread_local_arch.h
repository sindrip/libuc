#ifndef LIBUC_SRC_ARCH_X86_64_THREAD_LOCAL_ARCH_H
#define LIBUC_SRC_ARCH_X86_64_THREAD_LOCAL_ARCH_H

#include <stdckdint.h>
#include <stddef.h>

/* Variant 2 as the x86-64 psABI fixes it: compiled TP-relative accesses
 * assume the block ends block_size rounded up to its alignment below the
 * thread pointer, and the eight bytes at the thread pointer hold its own
 * value, read back for every address-of on a thread-local. */
constexpr size_t thread_local_arch_tcb_size = 8;

/* A block's geometry within one mapping, measured from a base the caller has
 * placed on a boundary of both the block's alignment and the TCB's. */
struct thread_local_arch_placement {
  size_t length;       /* bytes the mapping must span */
  size_t block_offset; /* where the initialization image is copied */
  size_t tp_offset;    /* the thread-pointer value */
};

/* block_size and alignment are whatever the executable's PT_TLS declared;
 * the sums fail rather than wrap. */
[[nodiscard]] static inline bool
thread_local_arch_place(size_t block_size, size_t alignment,
                        struct thread_local_arch_placement *placement) {
  size_t raised;
  if (ckd_add(&raised, block_size, alignment - 1)) {
    return false;
  }

  /* Raising the thread pointer to its own alignment pads below the block,
   * moving block and pointer together and keeping the distance the linker
   * resolved between them. */
  const size_t distance = raised & ~(alignment - 1);
  size_t tcb_raised;
  if (ckd_add(&tcb_raised, distance, alignof(void *) - 1)) {
    return false;
  }

  const size_t tp_offset = tcb_raised & ~(alignof(void *) - 1);
  size_t length;
  if (ckd_add(&length, tp_offset, thread_local_arch_tcb_size)) {
    return false;
  }

  *placement = (struct thread_local_arch_placement){
      .length = length,
      .block_offset = tp_offset - distance,
      .tp_offset = tp_offset,
  };
  return true;
}

#endif
