#ifndef LIBUC_SRC_ARCH_AARCH64_THREAD_LOCAL_ARCH_H
#define LIBUC_SRC_ARCH_AARCH64_THREAD_LOCAL_ARCH_H

#include <stdckdint.h>
#include <stddef.h>
#include <stdint.h>

/* Variant 1 as sysvabi64 fixes it: the thread pointer addresses the 16-byte
 * TCB, and compiled TP-relative accesses assume the block begins at tcb_size
 * rounded up to the block's alignment. */
constexpr size_t thread_local_tcb_size = 16;

/* A block's geometry within one mapping, measured from a base the caller has
 * placed on an alignment boundary. */
struct thread_local_placement {
  size_t length;       /* bytes the mapping must span */
  size_t block_offset; /* where the initialization image is copied */
  size_t tp_offset;    /* the thread-pointer value */
};

/* block_size and alignment are whatever the executable's PT_TLS declared;
 * the sums fail rather than wrap. */
[[nodiscard]] static inline bool
thread_local_place(size_t block_size, size_t alignment,
                   struct thread_local_placement *placement) {
  size_t raised;
  if (ckd_add(&raised, thread_local_tcb_size, alignment - 1)) {
    return false;
  }

  const size_t block_offset = raised & ~(alignment - 1);
  size_t length;
  if (ckd_add(&length, block_offset, block_size)) {
    return false;
  }

  *placement = (struct thread_local_placement){
      .length = length,
      .block_offset = block_offset,
      .tp_offset = 0,
  };
  return true;
}

/* tpidr_el0 is writable from EL0 unconditionally. */
[[nodiscard]] static inline bool
thread_local_install_available([[maybe_unused]] uintptr_t hwcap2) {
  return true;
}

static inline void thread_local_install(void *thread_pointer) {
  __asm__ volatile("msr tpidr_el0, %0" : : "r"(thread_pointer));
}

[[nodiscard]] static inline void *thread_local_read(void) {
  void *thread_pointer;
  __asm__ volatile("mrs %0, tpidr_el0" : "=r"(thread_pointer));
  return thread_pointer;
}

#endif
