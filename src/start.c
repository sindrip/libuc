#include <stdint.h>

#include "fiber/fiber.h"
#include "scheduler/scheduler.h"
#include "sys/auxv/auxv.h"
#include "thread_local/thread_local.h"

[[gnu::visibility("hidden")]] extern void (*const __libuc_init_array_start[])(
    void);
[[gnu::visibility("hidden")]] extern void (*const __libuc_init_array_end[])(
    void);

extern int main(int argc, char **argv, char **envp);

int __libuc_start(void *initial_stack);

constexpr size_t root_stack_length = (size_t)8 << 20;

struct root_arguments {
  char **argv;
  char **envp;
  int argument_count;
  uint32_t : 32;
};

static int root_entry(void *opaque) {
  struct root_arguments *arguments = opaque;

  for (void (*const *init)(void) = __libuc_init_array_start;
       init != __libuc_init_array_end; init++) {
    (*init)();
  }

  return main(arguments->argument_count, arguments->argv, arguments->envp);
}

/* The live startup frame becomes scheduler zero's control stack: parse what
 * the kernel handed over, build the root fiber, and run the reactor here. */
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
  if (__libuc_thread_local_layout() == nullptr) {
    return 127;
  }

  struct __libuc_scheduler scheduler;
  if (!__libuc_scheduler_become(&scheduler)) {
    return 127;
  }

  struct root_arguments arguments = {
      .argv = argv,
      .envp = envp,
      .argument_count = (int)argument_count,
  };
  struct __libuc_fiber *root =
      __libuc_fiber_spawn(root_stack_length, root_entry, &arguments);
  if (root == nullptr) {
    return 127;
  }

  __libuc_scheduler_enqueue(&scheduler, root);
  __libuc_scheduler_run(&scheduler);

  const int status = root->status;
  if (!__libuc_fiber_destroy(root)) {
    return 127;
  }

  return status;
}
