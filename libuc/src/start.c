/* The private bridge from the architecture entry point to the C program. */

#include <stdint.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-identifier"

/* The .init_array boundaries defined by libuc.ld. Implementation-namespace
 * names, because PROVIDE_HIDDEN yields to any definition from an input
 * object — an application symbol spelled the same way would silently
 * replace the boundaries, and the reserved namespace is the one place no
 * application identifier can legally live. Hidden visibility on the
 * declarations so the fixed executable addresses them directly instead of
 * through a GOT page. */
[[gnu::visibility("hidden")]] extern void (*const __libuc_init_array_start[])(void);
[[gnu::visibility("hidden")]] extern void (*const __libuc_init_array_end[])(void);

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

  for (void (*const *init)(void) = __libuc_init_array_start;
       init != __libuc_init_array_end; init++) {
    (*init)();
  }

  return main((int)argument_count, argv, envp);
}

#pragma clang diagnostic pop
