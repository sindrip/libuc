#include <string.h>

#include "thread_local/thread_local.h"

[[gnu::used]] static _Thread_local int initialized = 42;
[[gnu::used]] static _Thread_local int zeroed;

int main(void) {
  const struct __libuc_thread_local_layout *layout =
      __libuc_thread_local_layout();
  if (layout == nullptr || layout->image_size != sizeof(initialized) ||
      layout->block_size != sizeof(initialized) + sizeof(zeroed)) {
    return 124;
  }

  struct __libuc_thread_local_block first;
  struct __libuc_thread_local_block second;
  if (!__libuc_thread_local_block_create(&first) ||
      !__libuc_thread_local_block_create(&second)) {
    return 123;
  }

  int first_initialized = 0;
  int first_zeroed = 1;
  int second_initialized = 0;
  int second_zeroed = 1;
  memcpy(&first_initialized, first.block, sizeof(first_initialized));
  memcpy(&first_zeroed, first.block + sizeof(first_initialized),
         sizeof(first_zeroed));
  memcpy(&second_initialized, second.block, sizeof(second_initialized));
  memcpy(&second_zeroed, second.block + sizeof(second_initialized),
         sizeof(second_zeroed));

  const auto first_tcb =
      (const struct __libuc_thread_local_tcb *)first.thread_pointer;
  const auto second_tcb =
      (const struct __libuc_thread_local_tcb *)second.thread_pointer;
  const bool valid =
      first.thread_pointer != second.thread_pointer &&
      first_tcb->self == first_tcb && second_tcb->self == second_tcb &&
      first_tcb->fiber == nullptr && second_tcb->fiber == nullptr &&
      first_initialized == 42 && second_initialized == 42 &&
      first_zeroed == 0 && second_zeroed == 0;
  const bool destroyed = __libuc_thread_local_block_destroy(&first) &&
                         __libuc_thread_local_block_destroy(&second);
  return valid && destroyed ? 0 : 122;
}
