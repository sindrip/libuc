#include <stddef.h>

#include "thread_local/thread_local.h"

int main(int argc, char **argv, char **envp) {
  const struct __libuc_thread_local_layout *thread_local_layout =
      __libuc_thread_local_layout_get();
  if (thread_local_layout == nullptr || thread_local_layout->image != nullptr ||
      thread_local_layout->image_size != 0 ||
      thread_local_layout->block_size != 0 ||
      thread_local_layout->alignment != 1) {
    return 124;
  }

  if (!__libuc_thread_local_layout_init() ||
      __libuc_thread_local_layout_get() != thread_local_layout) {
    return 123;
  }

  if (argc < 1 || argv == nullptr || argv[0] == nullptr || envp == nullptr) {
    return 127;
  }

  const size_t argument_count = (size_t)argc;
  if (argv[argument_count] != nullptr || envp != &argv[argument_count + 1]) {
    return 126;
  }

  return 0;
}
