#include "thread_local.h"

#include <stdint.h>

#include <linux/elf.h>

#include <sys/auxv.h>

#include "auxv.h"

static struct __libuc_thread_local_image process_image = {
    .initialization = nullptr,
    .initialized_size = 0,
    .size = 0,
    .alignment = 1,
};
static bool process_image_initialized;

static_assert(sizeof(uintptr_t) == sizeof(Elf64_Addr));
static_assert(sizeof(size_t) == sizeof(Elf64_Xword));

static bool
thread_local_image_from_segment(const Elf64_Phdr *segment,
                                struct __libuc_thread_local_image *image) {
  if (segment->p_filesz > segment->p_memsz) {
    return false;
  }

  const size_t alignment =
      segment->p_align > 1 ? (size_t)segment->p_align : (size_t)1;
  if ((alignment & (alignment - 1)) != 0 ||
      segment->p_vaddr % alignment != segment->p_offset % alignment) {
    return false;
  }

  if (segment->p_filesz > UINTPTR_MAX - segment->p_vaddr ||
      segment->p_memsz > UINTPTR_MAX - segment->p_vaddr ||
      segment->p_filesz > UINT64_MAX - segment->p_offset) {
    return false;
  }

  *image = (struct __libuc_thread_local_image){
      .initialization =
          segment->p_filesz == 0
              ? nullptr
              : (const unsigned char *)(uintptr_t)segment->p_vaddr,
      .initialized_size = (size_t)segment->p_filesz,
      .size = (size_t)segment->p_memsz,
      .alignment = alignment,
  };
  return true;
}

bool __libuc_thread_local_image_init(void) {
  if (process_image_initialized) {
    return false;
  }

  uintptr_t program_headers_address;
  uintptr_t program_header_size;
  uintptr_t program_header_count;
  if (!__libuc_auxv_get(AT_PHDR, &program_headers_address) ||
      !__libuc_auxv_get(AT_PHENT, &program_header_size) ||
      !__libuc_auxv_get(AT_PHNUM, &program_header_count) ||
      program_headers_address == 0 ||
      program_headers_address % alignof(Elf64_Phdr) != 0 ||
      program_header_size != sizeof(Elf64_Phdr) || program_header_count == 0 ||
      program_header_count > SIZE_MAX / sizeof(Elf64_Phdr)) {
    return false;
  }

  const size_t table_size = (size_t)program_header_count * sizeof(Elf64_Phdr);
  if (table_size > UINTPTR_MAX - program_headers_address) {
    return false;
  }

  const Elf64_Phdr *program_headers =
      (const Elf64_Phdr *)program_headers_address;
  struct __libuc_thread_local_image image = {
      .initialization = nullptr,
      .initialized_size = 0,
      .size = 0,
      .alignment = 1,
  };
  bool found = false;

  for (size_t index = 0; index < (size_t)program_header_count; index++) {
    const Elf64_Phdr *header = &program_headers[index];
    if (header->p_type != PT_TLS) {
      continue;
    }
    if (found || !thread_local_image_from_segment(header, &image)) {
      return false;
    }
    found = true;
  }

  process_image = image;
  process_image_initialized = true;
  return true;
}

const struct __libuc_thread_local_image *__libuc_thread_local_image_get(void) {
  return &process_image;
}
