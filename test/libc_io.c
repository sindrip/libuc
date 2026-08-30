#include <stddef.h>
#include <stdint.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "fiber/fiber.h"
#include "scheduler/scheduler.h"
#include "thread_local/thread_local.h"
#include "unistd/rw_len.h"

static constexpr char payload[] = "ring-libc-io";
static int fds[2] = {-1, -1};
static bool reader_ok = true;
static bool writer_ok = true;

static void reader([[maybe_unused]] void *opaque) {
  reader_ok = reader_ok && pipe(fds) == 0;
  errno = 17;

  char buf[2 * sizeof(payload)];
  const ssize_t received = read(fds[0], buf, sizeof(buf));
  reader_ok = reader_ok && received == sizeof(payload);
  reader_ok = reader_ok && memcmp(buf, payload, sizeof(payload)) == 0;
  /* The writer's EBADF must not leak into this fiber's errno. */
  reader_ok = reader_ok && errno == 17;

  reader_ok = reader_ok && read(fds[0], buf, SIZE_MAX) == -1;
  reader_ok = reader_ok && errno == EINVAL;

  reader_ok = reader_ok && close(fds[0]) == 0 && close(fds[1]) == 0;
  reader_ok = reader_ok && read(fds[0], buf, sizeof(buf)) == -1;
  reader_ok = reader_ok && errno == EBADF;
}

static void writer([[maybe_unused]] void *opaque) {
  writer_ok = writer_ok && write(-1, payload, sizeof(payload)) == -1;
  writer_ok = writer_ok && errno == EBADF;

  errno = 29;
  writer_ok = writer_ok && write(fds[1], payload, sizeof(payload)) ==
                               (ssize_t)sizeof(payload);
  writer_ok = writer_ok && errno == 29;
}

int main(void) {
  /* 125 singles out an environment where user mode cannot install. */
  if (!__libuc_thread_local_install_available()) {
    return 125;
  }

  if (__libuc_rw_len(5) != 5 || __libuc_rw_len(SIZE_MAX >> 1) != INT32_MAX ||
      __libuc_rw_len((size_t)INT32_MAX + 1) != INT32_MAX) {
    return 124;
  }

  struct __libuc_scheduler scheduler;
  if (!__libuc_scheduler_become(&scheduler)) {
    return 123;
  }

  struct __libuc_fiber reads;
  struct __libuc_fiber writes;
  if (!__libuc_fiber_create(&reads, (size_t)256 * 1024, reader, nullptr) ||
      !__libuc_fiber_create(&writes, (size_t)256 * 1024, writer, nullptr)) {
    return 122;
  }

  __libuc_scheduler_enqueue(&scheduler, &reads);
  __libuc_scheduler_enqueue(&scheduler, &writes);
  __libuc_scheduler_run(&scheduler);

  if (!reader_ok) {
    return 121;
  }
  if (!writer_ok) {
    return 120;
  }
  if (scheduler.parked != 0 || scheduler.ready != 0 ||
      scheduler.ready_head != nullptr) {
    return 119;
  }
  if (!__libuc_fiber_destroy(&reads) || !__libuc_fiber_destroy(&writes)) {
    return 118;
  }

  return 0;
}
