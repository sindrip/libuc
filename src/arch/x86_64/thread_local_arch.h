#ifndef LIBUC_SRC_ARCH_X86_64_THREAD_LOCAL_ARCH_H
#define LIBUC_SRC_ARCH_X86_64_THREAD_LOCAL_ARCH_H

#include <stdckdint.h>
#include <stddef.h>
#include <stdint.h>

#include <asm/hwcap2.h>

/* Variant 2 as the x86-64 psABI fixes it: compiled TP-relative accesses
 * assume the block ends block_size rounded up to its alignment below the
 * thread pointer.  The self-pointer is at TP; libuc's second runtime word is
 * immediately above it and is outside the compiler's negative offsets. */
constexpr size_t thread_local_tcb_size = 16;

/* A block's geometry within one mapping, measured from a base the caller has
 * placed on a boundary of both the block's alignment and the TCB's. */
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
  if (ckd_add(&length, tp_offset, thread_local_tcb_size)) {
    return false;
  }

  *placement = (struct thread_local_placement){
      .length = length,
      .block_offset = tp_offset - distance,
      .tp_offset = tp_offset,
  };
  return true;
}

/* Writing the FS base from user mode takes the FSGSBASE instructions, which
 * trap unless the kernel enabled CR4.FSGSBASE and said so via AT_HWCAP2. The
 * syscall fallback would be arch_prctl, which the permitted direct-syscall
 * list excludes. */
[[nodiscard]] static inline bool
thread_local_install_available(uintptr_t hwcap2) {
  return (hwcap2 & HWCAP2_FSGSBASE) != 0;
}

static inline void thread_local_install(void *thread_pointer) {
  __asm__ volatile("wrfsbase %0" : : "r"(thread_pointer));
}

[[nodiscard]] static inline void *thread_local_read(void) {
  void *thread_pointer;
  __asm__ volatile("rdfsbase %0" : "=r"(thread_pointer));
  return thread_pointer;
}

#endif
