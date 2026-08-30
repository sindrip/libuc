#include "thread_local.h"

#include <stdckdint.h>
#include <stdint.h>

#include <linux/auxvec.h>
#include <linux/elf.h>
#include <linux/mman.h>

#include <string.h>

#include "sys/auxv/auxv.h"
#include "syscall.h"
#include "thread_local_arch.h"

static_assert(sizeof(uintptr_t) == sizeof(Elf64_Addr));
static_assert(sizeof(size_t) == sizeof(Elf64_Xword));

const struct __libuc_thread_local_layout *__libuc_thread_local_layout(void) {
  static struct __libuc_thread_local_layout process_layout;
  static const struct __libuc_thread_local_layout *layout = nullptr;

  if (layout != nullptr) {
    return layout;
  }

  /* Read the final executable layout from auxv. PT_TLS is the
   * thread-local metadata ABI. */
  uintptr_t address;
  uintptr_t entry_size;
  uintptr_t entry_count;
  if (!__libuc_auxv_get(AT_PHDR, &address) ||
      !__libuc_auxv_get(AT_PHENT, &entry_size) ||
      !__libuc_auxv_get(AT_PHNUM, &entry_count)) {
    return nullptr;
  }

  if (address == 0 || address % alignof(Elf64_Phdr) != 0 ||
      entry_size != sizeof(Elf64_Phdr) || entry_count == 0) {
    return nullptr;
  }

  const size_t header_count = (size_t)entry_count;
  size_t table_size;
  uintptr_t table_end;
  if (ckd_mul(&table_size, header_count, sizeof(Elf64_Phdr)) ||
      ckd_add(&table_end, address, table_size)) {
    return nullptr;
  }

  const auto headers = (const Elf64_Phdr *)address;

  /* Find the executable's sole PT_TLS segment. Absence is valid; a second
   * segment makes the image ambiguous. */
  const Elf64_Phdr *segment = nullptr;
  for (size_t index = 0; index < header_count; index++) {
    const auto header = &headers[index];
    if (header->p_type != PT_TLS) {
      continue;
    }
    if (segment != nullptr) {
      return nullptr;
    }
    segment = header;
  }

  /* Decode the absent segment explicitly as the canonical empty layout. */
  if (segment == nullptr) {
    process_layout = (struct __libuc_thread_local_layout){
        .image = nullptr,
        .image_size = 0,
        .block_size = 0,
        .alignment = 1,
    };
  } else {
    /* Validate the image dimensions, mapped range, and effective alignment. */
    Elf64_Addr segment_end;
    if (segment->p_filesz > segment->p_memsz ||
        ckd_add(&segment_end, segment->p_vaddr, segment->p_memsz)) {
      return nullptr;
    }

    const Elf64_Xword alignment = segment->p_align == 0 ? 1 : segment->p_align;
    if (__builtin_popcountg(alignment) != 1) {
      return nullptr;
    }

    const unsigned char *image =
        segment->p_filesz == 0
            ? nullptr
            : (const unsigned char *)(uintptr_t)segment->p_vaddr;
    process_layout = (struct __libuc_thread_local_layout){
        .image = image,
        .image_size = (size_t)segment->p_filesz,
        .block_size = (size_t)segment->p_memsz,
        .alignment = (size_t)alignment,
    };
  }

  /* Make the decoded layout available. */
  layout = &process_layout;
  return layout;
}

[[nodiscard]] bool
__libuc_thread_local_block_create(struct __libuc_thread_local_block *block) {
  const struct __libuc_thread_local_layout *layout =
      __libuc_thread_local_layout();
  if (layout == nullptr) {
    return false;
  }

  struct thread_local_placement placement;
  if (!thread_local_place(layout->block_size, layout->alignment, &placement)) {
    return false;
  }

  /* The placements measure their offsets from a base on a boundary of both
   * the block's alignment and the TCB's, so the base carries whichever is
   * coarser. Raising it keeps the block's alignment, which divides it. */
  const size_t base_alignment =
      layout->alignment < alignof(struct __libuc_thread_local_tcb)
          ? alignof(struct __libuc_thread_local_tcb)
          : layout->alignment;

  size_t mapping_length;
  if (ckd_add(&mapping_length, placement.length, base_alignment - 1)) {
    return false;
  }

  const long raw_mapping =
      __libuc_sys_mmap(nullptr, mapping_length, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (__libuc_syscall_failed(raw_mapping)) {
    return false;
  }

  const uintptr_t raw_address = (uintptr_t)raw_mapping;
  uintptr_t rounded_address;
  if (ckd_add(&rounded_address, raw_address, base_alignment - 1)) {
    (void)__libuc_sys_munmap((void *)raw_address, mapping_length);
    return false;
  }
  const uintptr_t address = rounded_address & ~(uintptr_t)(base_alignment - 1);
  unsigned char *base = (unsigned char *)address;
  void *thread_pointer = base + placement.tp_offset;
  unsigned char *tls_block = base + placement.block_offset;
  struct __libuc_thread_local_tcb *tcb = thread_pointer;

  if (layout->block_size != 0) {
    memset(tls_block, 0, layout->block_size);
    if (layout->image_size != 0) {
      memcpy(tls_block, layout->image, layout->image_size);
    }
  }
  tcb->self = tcb;
  tcb->fiber = nullptr;

  *block = (struct __libuc_thread_local_block){
      .mapping = (void *)raw_address,
      .mapping_length = mapping_length,
      .block = tls_block,
      .thread_pointer = thread_pointer,
  };
  return true;
}

[[nodiscard]] bool __libuc_thread_local_block_destroy(
    const struct __libuc_thread_local_block *block) {
  return !__libuc_syscall_failed(
      __libuc_sys_munmap(block->mapping, block->mapping_length));
}

[[nodiscard]] bool __libuc_thread_local_install_available(void) {
  uintptr_t hwcap2;
  if (!__libuc_auxv_get(AT_HWCAP2, &hwcap2)) {
    hwcap2 = 0;
  }
  return thread_local_install_available(hwcap2);
}

void __libuc_thread_local_block_install(
    const struct __libuc_thread_local_block *block) {
  thread_local_install(block->thread_pointer);
}
