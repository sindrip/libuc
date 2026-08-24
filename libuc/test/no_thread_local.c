#include <stddef.h>

#include "thread_local/thread_local.h"

int main(int argc, char **argv, char **envp) {
  const struct __libuc_thread_local_image *thread_local_image =
      __libuc_thread_local_image_get();
  if (thread_local_image->initialization != nullptr ||
      thread_local_image->initialized_size != 0 ||
      thread_local_image->size != 0 || thread_local_image->alignment != 1) {
    return 124;
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
