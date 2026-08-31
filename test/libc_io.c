#include <stddef.h>
#include <stdint.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <threads.h>

#include "unistd/rw_len.h"

static constexpr char payload[] = "ring-libc-io";
static int fds[2] = {-1, -1};
static bool reader_ok = true;
static bool writer_ok = true;

static int reader([[maybe_unused]] void *opaque) {
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

  return 0;
}

static int writer([[maybe_unused]] void *opaque) {
  writer_ok = writer_ok && write(-1, payload, sizeof(payload)) == -1;
  writer_ok = writer_ok && errno == EBADF;

  errno = 29;
  writer_ok = writer_ok && write(fds[1], payload, sizeof(payload)) ==
                               (ssize_t)sizeof(payload);
  writer_ok = writer_ok && errno == 29;

  return 0;
}

int main(void) {
  if (__libuc_rw_len(5) != 5 || __libuc_rw_len(SIZE_MAX >> 1) != INT32_MAX ||
      __libuc_rw_len((size_t)INT32_MAX + 1) != INT32_MAX) {
    return 124;
  }

  thrd_t reads;
  thrd_t writes;
  if (thrd_create(&reads, reader, nullptr) != thrd_success ||
      thrd_create(&writes, writer, nullptr) != thrd_success) {
    return 123;
  }
  if (thrd_join(reads, nullptr) != thrd_success ||
      thrd_join(writes, nullptr) != thrd_success) {
    return 122;
  }

  if (!reader_ok) {
    return 121;
  }
  if (!writer_ok) {
    return 120;
  }

  return 0;
}
