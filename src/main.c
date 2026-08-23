#include <stddef.h>

#include <asm/byteorder.h>
#include <linux/in.h>

#include "auxv.h"
#include "crash.h"
#include "fiber.h"
#include "io.h"
#include "ring.h"
#include "scheduler.h"
#include "syscall.h"

static void put(char c) { raw_write(1, &c, 1); }

static void put_str(const char *s) {
  size_t n = 0;
  while (s[n]) {
    n++;
  }
  raw_write(1, s, n);
}

static void put_dec(unsigned long v) {
  char buf[20];
  char *p = buf + sizeof buf;

  do {
    *--p = (char)('0' + v % 10);
    v /= 10;
  } while (v);

  raw_write(1, p, (size_t)(buf + sizeof buf - p));
}

static void abc_fiber([[maybe_unused]] void *arg) {
  put('A');
  rt_fiber_yield();
  put('B');
  rt_fiber_yield();
  put('C');
}

static void rt004_selftest(struct rt_scheduler *s) {
  struct rt_fiber t;
  rt_fiber_create(&t, abc_fiber, nullptr);

  int count = 0;
  while (t.request.kind != RT_REQUEST_EXIT) {
    count++;
    put((char)('0' + count));
    rt_scheduler_resume(s, &t);
  }
  put('\n');
}

static void rt005_probe(void) {
  struct io_uring_query_opcode q = {};

  auto ret = rt_ring_probe(&q);
  if (sys_failed(ret)) {
    put_str("probe failed: ");
    put_dec((unsigned long)-ret);
    put('\n');
    return;
  }

  put_str("ops ");
  put_dec(q.nr_request_opcodes);
  put_str(" reg ");
  put_dec(q.nr_register_opcodes);
  put_str(" feat ");
  put_dec(q.feature_flags);
  put('\n');
}

static void rt005_setup_failure(void) {

  struct io_uring_params params = {.flags = IORING_SETUP_SQPOLL |
                                            IORING_SETUP_DEFER_TASKRUN};
  auto ret = sys_io_uring_setup(8, &params);
  if (!sys_failed(ret)) {

    put_str("setup accepted bogus flags\n");
    return;
  }

  put_str("setup rejects bogus flags: ");
  put_dec((unsigned long)-ret);
  put('\n');
}

static constexpr __u64 NOP_SENTINEL = 0xF00D'FACE'DEAD'BEEF;

static void rt005_nop(void) {
  struct rt_ring ring;

  auto ret = rt_ring_setup(&ring, 8);
  if (sys_failed(ret)) {
    put_str("nop: setup failed: ");
    put_dec((unsigned long)-ret);
    put('\n');
    return;
  }

  struct io_uring_sqe *sqe = rt_ring_sqe(&ring);
  if (sqe == nullptr) {
    put_str("nop: sq full on a fresh ring\n");
    return;
  }
  sqe->opcode = IORING_OP_NOP;
  sqe->user_data = NOP_SENTINEL;

  ret = rt_ring_submit_and_wait(&ring, 1, 1);
  if (sys_failed(ret)) {
    put_str("nop: enter failed: ");
    put_dec((unsigned long)-ret);
    put('\n');
    return;
  }
  if (ret != 1) {
    put_str("nop: enter consumed ");
    put_dec((unsigned long)ret);
    put('\n');
    return;
  }

  struct io_uring_cqe cqe;
  if (!rt_ring_reap(&ring, &cqe)) {
    put_str("nop: cq empty after wait\n");
    return;
  }
  if (cqe.res != 0) {
    put_str("nop: res ");
    put_dec((unsigned long)(cqe.res < 0 ? -cqe.res : cqe.res));
    put('\n');
    return;
  }
  if (cqe.user_data != NOP_SENTINEL) {
    put_str("nop: user_data ");
    put_dec((unsigned long)cqe.user_data);
    put('\n');
    return;
  }

  put_str("nop ok\n");
}

static void hello_fiber(void *arg) {
  const char *msg = arg;

  auto res = rt_nop();
  if (res != 0) {
    put_str("fiber: nop res ");
    put_dec((unsigned long)(res < 0 ? -res : res));
    put('\n');
    return;
  }

  size_t n = 0;
  while (msg[n]) {
    n++;
  }
  res = rt_write(1, msg, (unsigned)n);
  if (res != (int)n) {
    put_str("fiber: write res ");
    put_dec((unsigned long)(res < 0 ? -res : res));
    put('\n');
  }
}

static constexpr int RT_AF_INET = 2;
static constexpr int RT_SOCK_STREAM = 1;
static constexpr int RT_IPPROTO_TCP = 6;

static void echo_once(void) {
  auto fd = rt_socket(RT_AF_INET, RT_SOCK_STREAM, RT_IPPROTO_TCP);

  if (fd < 0) {
    put_str("socket: create failed: ");
    put_dec((unsigned long)-fd);
    put('\n');
    return;
  }

  struct sockaddr_in addr = {
      .sin_family = RT_AF_INET,
      .sin_port = __constant_htons(8080),
      .sin_addr = {.s_addr = INADDR_ANY},
  };

  auto bind_res = rt_bind(fd, &addr, (unsigned)sizeof addr);
  if (bind_res != 0) {
    put_str("socket: bind returned ");
    if (bind_res < 0) {
      put_str("-");
      put_dec((unsigned long)-(long)bind_res);
    } else {
      put_dec((unsigned long)bind_res);
    }
    put('\n');
  }

  auto listen_res = 0;
  if (bind_res == 0) {
    listen_res = rt_listen(fd, 8);
    if (listen_res != 0) {
      put_str("socket: listen returned ");
      if (listen_res < 0) {
        put_str("-");
        put_dec((unsigned long)-(long)listen_res);
      } else {
        put_dec((unsigned long)listen_res);
      }
      put('\n');
    }
  }

  auto client_fd = -1;
  if (bind_res == 0 && listen_res == 0) {
    client_fd = rt_accept(fd);
    if (client_fd < 0) {
      put_str("socket: accept failed: ");
      put_dec((unsigned long)-(long)client_fd);
      put('\n');
    }
  }

  char buf[256];
  auto recv_res = 0;
  if (client_fd >= 0) {
    recv_res = rt_recv(client_fd, buf, (unsigned)sizeof buf);
    if (recv_res <= 0) {
      put_str("socket: recv returned ");
      if (recv_res < 0) {
        put_str("-");
        put_dec((unsigned long)-(long)recv_res);
      } else {
        put_dec((unsigned long)recv_res);
      }
      put('\n');
    }
  }

  unsigned sent = 0;
  while (recv_res > 0 && sent < (unsigned)recv_res) {
    auto send_res =
        rt_send(client_fd, buf + sent, (unsigned)recv_res - sent);
    if (send_res <= 0) {
      put_str("socket: send returned ");
      if (send_res < 0) {
        put_str("-");
        put_dec((unsigned long)-(long)send_res);
      } else {
        put_dec((unsigned long)send_res);
      }
      put('\n');
      break;
    }
    sent += (unsigned)send_res;
  }

  auto client_close_res = 0;
  if (client_fd >= 0) {
    client_close_res = rt_close(client_fd);
    if (client_close_res != 0) {
      put_str("socket: client close returned ");
      if (client_close_res < 0) {
        put_str("-");
        put_dec((unsigned long)-(long)client_close_res);
      } else {
        put_dec((unsigned long)client_close_res);
      }
      put('\n');
    }
  }

  auto close_res = rt_close(fd);
  if (close_res != 0) {
    put_str("socket: close returned ");
    if (close_res < 0) {
      put_str("-");
      put_dec((unsigned long)-(long)close_res);
    } else {
      put_dec((unsigned long)close_res);
    }
    put('\n');
    return;
  }

  if (bind_res != 0 || listen_res != 0 || client_fd < 0 || recv_res <= 0 ||
      sent != (unsigned)recv_res || client_close_res != 0) {
    return;
  }

  static constexpr char success_msg[] = "echo ok\n";
  static constexpr unsigned success_len = (unsigned)(sizeof success_msg - 1);

  auto written = rt_write(1, success_msg, success_len);
  if (written != (int)success_len) {
    put_str("socket: write returned ");
    if (written < 0) {
      put_str("-");
      put_dec((unsigned long)-(long)written);
    } else {
      put_dec((unsigned long)written);
    }
    put_str(", expected ");
    put_dec(success_len);
    put('\n');
  }
}

static struct rt_fiber_queue park_q;

static void parker_fiber([[maybe_unused]] void *arg) {
  rt_fiber_park(&park_q);
  put_str("park/wake ok\n");
}

static void waker_fiber([[maybe_unused]] void *arg) {
  struct rt_fiber *f = rt_fiber_queue_pop(&park_q);
  if (f == nullptr) {
    put_str("park: nobody parked\n");
    return;
  }
  rt_fiber_wake(f);
}

static void rt009_park(struct rt_scheduler *s) {
  struct rt_fiber parker;
  struct rt_fiber waker;

  rt_scheduler_spawn(s, &parker, parker_fiber, nullptr);
  rt_scheduler_spawn(s, &waker, waker_fiber, nullptr);
  rt_scheduler_run(s);
}

static volatile bool echo_finished;

static void yield_fiber([[maybe_unused]] void *arg) {
  while (!echo_finished) {
    rt_fiber_yield();
  }
}

static void socket_fiber([[maybe_unused]] void *arg) {
  echo_once();
  echo_finished = true;
}

static void rt006_demo(struct rt_scheduler *s) {

  static char msg_a[] = "hello a\n";
  static char msg_b[] = "hello b\n";
  struct rt_fiber a;
  struct rt_fiber b;
  rt_scheduler_spawn(s, &a, hello_fiber, msg_a);
  rt_scheduler_spawn(s, &b, hello_fiber, msg_b);

  rt_scheduler_run(s);

  struct rt_fiber socket;
  struct rt_fiber yielder;
  rt_scheduler_spawn(s, &socket, socket_fiber, nullptr);
  rt_scheduler_spawn(s, &yielder, yield_fiber, nullptr);
  rt_scheduler_run(s);
}

[[noreturn]] void rt_main(void *stack);

[[noreturn]] void rt_main(void *stack) {

  rt_crash_install();

  rt_auxv_init(stack);

  struct rt_scheduler sched;
  auto init = rt_scheduler_init(&sched, 8);
  if (sys_failed(init)) {
    put_str("scheduler init failed: ");
    put_dec((unsigned long)-init);
    put('\n');
    for (;;) {
      __asm__ volatile("wfe");
    }
  }

  static volatile bool verbose = false;
  if (verbose) {
    static constexpr char banner[] = "rt: alive\n";
    raw_write(1, banner, sizeof banner - 1);
    rt004_selftest(&sched);
    rt005_probe();
    rt005_setup_failure();
    rt005_nop();
    rt009_park(&sched);
  }
  rt006_demo(&sched);

  for (;;) {
    __asm__ volatile("wfe");
  }
}
