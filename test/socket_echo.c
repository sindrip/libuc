#include <stdint.h>

#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "fiber/fiber.h"
#include "scheduler/scheduler.h"
#include "thread_local/thread_local.h"

static_assert(sizeof(struct sockaddr) == __SOCK_SIZE__);

static constexpr uint16_t echo_port = 7777;

static bool socket_ok = true;

static void check(bool ok) { socket_ok = socket_ok && ok; }

static struct sockaddr_in loopback(void) {
  return (struct sockaddr_in){
      .sin_family = AF_INET,
      .sin_port = __builtin_bswap16(echo_port),
      .sin_addr = {__builtin_bswap32(INADDR_LOOPBACK)},
  };
}

static void prober([[maybe_unused]] void *opaque) {
  errno = 11;

  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  check(fd >= 0);
  check(errno == 11);
  check(close(fd) == 0);

  check(socket(-1, SOCK_STREAM, 0) == -1);
  check(errno == EAFNOSUPPORT);

  const struct sockaddr_in address = loopback();
  const int server = socket(AF_INET, SOCK_STREAM, 0);
  check(server >= 0);
  errno = 11;
  check(bind(server, (const struct sockaddr *)&address, sizeof(address)) == 0);
  check(listen(server, 1) == 0);
  check(errno == 11);

  const int squatter = socket(AF_INET, SOCK_STREAM, 0);
  check(squatter >= 0);
  check(bind(squatter, (const struct sockaddr *)&address, sizeof(address)) ==
        -1);
  check(errno == EADDRINUSE);

  check(close(squatter) == 0);
  check(close(server) == 0);
}

int main(void) {
  /* 125 singles out an environment where user mode cannot install. */
  if (!__libuc_thread_local_install_available()) {
    return 125;
  }

  struct __libuc_scheduler scheduler;
  if (!__libuc_scheduler_become(&scheduler)) {
    return 124;
  }

  struct __libuc_fiber fiber;
  if (!__libuc_fiber_create(&fiber, (size_t)256 * 1024, prober, nullptr)) {
    return 123;
  }

  __libuc_scheduler_enqueue(&scheduler, &fiber);
  __libuc_scheduler_run(&scheduler);

  if (!socket_ok) {
    return 122;
  }
  if (scheduler.parked != 0 || scheduler.ready != 0 ||
      scheduler.ready_head != nullptr) {
    return 121;
  }
  if (!__libuc_fiber_destroy(&fiber)) {
    return 120;
  }

  return 0;
}
