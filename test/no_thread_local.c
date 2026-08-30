#include <stddef.h>

#include "fiber/fiber.h"
#include "thread_local/thread_local.h"

static struct __libuc_fiber *seen_current;

static void record_current([[maybe_unused]] void *opaque) {
  seen_current = __libuc_fiber_current();
}

int main(int argc, char **argv, char **envp) {
  const struct __libuc_thread_local_layout *thread_local_layout =
      __libuc_thread_local_layout();
  if (thread_local_layout == nullptr || thread_local_layout->image != nullptr ||
      thread_local_layout->image_size != 0 ||
      thread_local_layout->block_size != 0 ||
      thread_local_layout->alignment != 1) {
    return 124;
  }

  if (__libuc_thread_local_layout() != thread_local_layout) {
    return 123;
  }

  struct __libuc_thread_local_block first;
  struct __libuc_thread_local_block second;
  if (!__libuc_thread_local_block_create(&first) ||
      !__libuc_thread_local_block_create(&second)) {
    return 122;
  }
  const auto first_tcb =
      (const struct __libuc_thread_local_tcb *)first.thread_pointer;
  const auto second_tcb =
      (const struct __libuc_thread_local_tcb *)second.thread_pointer;
  const bool blocks_ready =
      first.thread_pointer != second.thread_pointer &&
      first_tcb->self == first_tcb && second_tcb->self == second_tcb &&
      first_tcb->fiber == nullptr && second_tcb->fiber == nullptr;
  const bool destroyed = __libuc_thread_local_block_destroy(&first) &&
                         __libuc_thread_local_block_destroy(&second);
  if (!blocks_ready || !destroyed) {
    return 121;
  }

  if (__libuc_thread_local_install_available()) {
    struct __libuc_fiber *root = __libuc_fiber_current();
    if (root == nullptr) {
      return 119;
    }

    struct __libuc_fiber fiber;
    if (!__libuc_fiber_create(&fiber, (size_t)256 * 1024, record_current,
                              nullptr)) {
      return 118;
    }

    if (__libuc_fiber_resume(&fiber) != __LIBUC_FIBER_REQUEST_EXIT) {
      return 116;
    }
    const bool carried =
        seen_current == &fiber && __libuc_fiber_current() == root;

    if (!__libuc_fiber_destroy(&fiber) || !carried) {
      return 117;
    }
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
