#include <stdint.h>

#include "auxv.h"
#include "thread_local/thread_local.h"

[[gnu::visibility("hidden")]] extern void (*const __libuc_init_array_start[])(
    void);
[[gnu::visibility("hidden")]] extern void (*const __libuc_init_array_end[])(
    void);

extern int main(int argc, char **argv, char **envp);

int __libuc_start(void *initial_stack);

int __libuc_start(void *initial_stack) {
  uintptr_t *words = initial_stack;
  const uintptr_t argument_count = words[0];

  if (argument_count > (uintptr_t)__INT_MAX__) {
    return 127;
  }

  char **argv = (char **)&words[1];
  if (argv[argument_count] != nullptr) {
    return 127;
  }

  char **envp = &argv[argument_count + 1];
  char **environment_end = envp;
  while (*environment_end != nullptr) {
    environment_end++;
  }

  __libuc_auxv_init((const uintptr_t *)(environment_end + 1));
  if (!__libuc_thread_local_image_init()) {
    return 127;
  }

  for (void (*const *init)(void) = __libuc_init_array_start;
       init != __libuc_init_array_end; init++) {
    (*init)();
  }

  return main((int)argument_count, argv, envp);
}
