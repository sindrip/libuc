#include <stdint.h>

#include "auxv.h"
#include "fiber/fiber.h"
#include "scheduler/scheduler.h"
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
  int status;
};

static void root_entry(void *opaque) {
  struct root_arguments *arguments = opaque;

  for (void (*const *init)(void) = __libuc_init_array_start;
       init != __libuc_init_array_end; init++) {
    (*init)();
  }

  arguments->status =
      main(arguments->argument_count, arguments->argv, arguments->envp);
}

/* The kernel stack is bootstrap storage only: parse what the kernel handed
 * over, build the root fiber, and carry main's status to exit_group. */
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
      .status = 127,
  };
  struct __libuc_fiber root;
  if (!__libuc_fiber_create(&root, root_stack_length, root_entry, &arguments)) {
    return 127;
  }

  __libuc_scheduler_enqueue(&scheduler, &root);
  __libuc_scheduler_run(&scheduler);

  if (!__libuc_fiber_destroy(&root)) {
    return 127;
  }
  return arguments.status;
}
