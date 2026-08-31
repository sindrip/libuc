#include <stdint.h>

#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "fiber/fiber.h"
#include "scheduler/scheduler.h"
#include "thread_local/thread_local.h"

static_assert(sizeof(struct sockaddr) == __SOCK_SIZE__);

static constexpr uint16_t echo_port = 7777;
static constexpr char payload[] = "ring-echo";

static bool server_ok = true;
static bool client_ok = true;

static struct sockaddr_in loopback(uint16_t port) {
  return (struct sockaddr_in){
      .sin_family = AF_INET,
      .sin_port = __builtin_bswap16(port),
      .sin_addr = {__builtin_bswap32(INADDR_LOOPBACK)},
  };
}

static int server(void *opaque) {
  bool *ok = opaque;

  *ok = *ok && socket(-1, SOCK_STREAM, 0) == -1;
  *ok = *ok && errno == EAFNOSUPPORT;

  const struct sockaddr_in address = loopback(echo_port);
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  *ok = *ok && fd >= 0;
  errno = 17;
  *ok =
      *ok && bind(fd, (const struct sockaddr *)&address, sizeof(address)) == 0;
  *ok = *ok && listen(fd, 1) == 0;

  const int squatter = socket(AF_INET, SOCK_STREAM, 0);
  *ok = *ok && bind(squatter, (const struct sockaddr *)&address,
                    sizeof(address)) == -1;
  *ok = *ok && errno == EADDRINUSE;
  *ok = *ok && close(squatter) == 0;

  errno = 17;
  struct sockaddr_in peer;
  socklen_t peer_length = sizeof(peer);
  const int conn = accept(fd, (struct sockaddr *)&peer, &peer_length);
  *ok = *ok && conn >= 0;
  *ok = *ok && peer_length == sizeof(peer) && peer.sin_family == AF_INET;
  /* The client's ECONNREFUSED landed while this fiber was parked. */
  *ok = *ok && errno == 17;

  char buf[2 * sizeof(payload)];
  *ok = *ok && read(conn, buf, sizeof(buf)) == sizeof(payload);
  *ok = *ok && memcmp(buf, payload, sizeof(payload)) == 0;
  *ok = *ok && write(conn, buf, sizeof(payload)) == sizeof(payload);

  *ok = *ok && close(conn) == 0 && close(fd) == 0;

  return 0;
}

static int client(void *opaque) {
  bool *ok = opaque;

  const struct sockaddr_in nobody = loopback(echo_port + 1);
  const int refused = socket(AF_INET, SOCK_STREAM, 0);
  *ok = *ok && refused >= 0;
  *ok = *ok && connect(refused, (const struct sockaddr *)&nobody,
                       sizeof(nobody)) == -1;
  *ok = *ok && errno == ECONNREFUSED;
  *ok = *ok && close(refused) == 0;

  const struct sockaddr_in address = loopback(echo_port);
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  *ok = *ok && fd >= 0;
  errno = 29;
  *ok = *ok &&
        connect(fd, (const struct sockaddr *)&address, sizeof(address)) == 0;
  *ok = *ok && write(fd, payload, sizeof(payload)) == sizeof(payload);

  char buf[2 * sizeof(payload)];
  *ok = *ok && read(fd, buf, sizeof(buf)) == sizeof(payload);
  *ok = *ok && memcmp(buf, payload, sizeof(payload)) == 0;
  *ok = *ok && errno == 29;

  *ok = *ok && close(fd) == 0;

  return 0;
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

  struct __libuc_fiber *serves;
  struct __libuc_fiber *connects;
  if ((serves = __libuc_fiber_spawn((size_t)256 * 1024, server, &server_ok)) ==
          nullptr ||
      (connects = __libuc_fiber_spawn((size_t)256 * 1024, client,
                                      &client_ok)) == nullptr) {
    return 123;
  }

  __libuc_scheduler_enqueue(&scheduler, serves);
  __libuc_scheduler_enqueue(&scheduler, connects);
  __libuc_scheduler_run(&scheduler);

  if (!server_ok) {
    return 122;
  }
  if (!client_ok) {
    return 121;
  }
  if (scheduler.parked_count != 0 || scheduler.ready_count != 0 ||
      scheduler.ready_head != nullptr) {
    return 120;
  }
  if (!__libuc_fiber_destroy(serves) || !__libuc_fiber_destroy(connects)) {
    return 119;
  }

  return 0;
}
