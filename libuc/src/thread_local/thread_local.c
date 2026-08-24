#include "thread_local.h"

#include <stdint.h>

#include <linux/elf.h>

#include <sys/auxv.h>

#include "auxv.h"

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
      entry_size != sizeof(Elf64_Phdr) || entry_count == 0 ||
      entry_count > SIZE_MAX / sizeof(Elf64_Phdr)) {
    return nullptr;
  }

  const size_t header_count = (size_t)entry_count;
  const size_t table_size = header_count * sizeof(Elf64_Phdr);
  if (table_size > UINTPTR_MAX - address) {
    return nullptr;
  }

  const Elf64_Phdr *headers = (const Elf64_Phdr *)address;

  /* Find the executable's sole PT_TLS segment. Absence is valid; a second
   * segment makes the image ambiguous. */
  const Elf64_Phdr *segment = nullptr;
  for (size_t index = 0; index < header_count; index++) {
    const Elf64_Phdr *header = &headers[index];
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
    if (segment->p_filesz > segment->p_memsz ||
        segment->p_memsz > UINT64_MAX - segment->p_vaddr) {
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
