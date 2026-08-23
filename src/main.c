#include <stddef.h>
#include <stdint.h>

#include <asm/byteorder.h>
#include <linux/in.h>

#include "auxv.h"
#include "crash.h"
#include "fiber.h"
#include "io.h"
#include "ring.h"
#include "scheduler.h"
#include "syscall.h"
#include "time.h"

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

static unsigned long bench_cns(unsigned long ticks, unsigned long iters) {
  auto hz = rt_tick_hz();
  if (hz == 0 || iters == 0) {
    return 0;
  }
  return (ticks * 100000000000UL) / (hz * iters);
}

static void bench_report(const char *name, unsigned long ticks,
                         unsigned long iters) {
  auto cns = bench_cns(ticks, iters);
  put_str(name);
  put_str(" ");
  put_dec(cns / 100);
  put('.');
  auto frac = cns % 100;
  if (frac < 10) {
    put('0');
  }
  put_dec(frac);
  put_str(" ns/op\n");
}

static constexpr unsigned long BENCH_YIELD = 200000;
static constexpr unsigned long BENCH_NOP = 20000;
static constexpr unsigned long BENCH_CURRENT = 1000000;
static constexpr unsigned long BENCH_SPAWN = 20000;

static unsigned long bench_yield_ticks;
static unsigned long bench_nop_ticks;
static unsigned long bench_current_ticks;
static unsigned long bench_clock_ticks;
static unsigned long bench_spawn_ticks;
static unsigned long bench_wake_ticks;
static unsigned long bench_syscall_ticks;
static struct rt_fiber_queue bench_empty_q;
static unsigned long bench_sink;
static unsigned long bench_spawned;

static void bench_child([[maybe_unused]] void *arg) { bench_spawned++; }

static constexpr unsigned long BENCH_BATCH_OPS = 5000;

static void bench_batch_fiber([[maybe_unused]] void *arg) {
  for (unsigned long i = 0; i < BENCH_BATCH_OPS; i++) {
    if (rt_nop() != 0) {
      put_str("bench: batch nop failed\n");
      return;
    }
  }
}

static void bench_batch(struct rt_scheduler *s, unsigned long k) {
  auto t0 = rt_ticks();
  for (unsigned long i = 0; i < k; i++) {
    if (rt_scheduler_spawn(s, bench_batch_fiber, nullptr) != 0) {
      put_str("bench: batch spawn failed\n");
      return;
    }
  }
  rt_scheduler_run(s);
  auto ticks = rt_ticks() - t0;

  put_str("bench: nop, ");
  put_dec(k);
  put_str(k < 10 ? " fiber  in flight  " : " fibers in flight  ");
  bench_report("", ticks, k * BENCH_BATCH_OPS);
}

static void bench_fiber([[maybe_unused]] void *arg) {
  auto t0 = rt_ticks();
  for (unsigned long i = 0; i < BENCH_YIELD; i++) {
    rt_fiber_yield();
  }
  bench_yield_ticks = rt_ticks() - t0;

  t0 = rt_ticks();
  for (unsigned long i = 0; i < BENCH_NOP; i++) {
    auto r = rt_nop();
    if (r != 0) {
      put_str("bench: nop failed\n");
      return;
    }
  }
  bench_nop_ticks = rt_ticks() - t0;

  t0 = rt_ticks();
  for (unsigned long i = 0; i < BENCH_CURRENT; i++) {
    struct rt_fiber *self = rt_fiber_current();
    bench_sink += (unsigned long)(uintptr_t)self;
  }
  bench_current_ticks = rt_ticks() - t0;

  t0 = rt_ticks();
  for (unsigned long i = 0; i < BENCH_CURRENT; i++) {
    bench_sink += rt_ticks();
  }
  bench_clock_ticks = rt_ticks() - t0;

  t0 = rt_ticks();
  for (unsigned long i = 0; i < BENCH_NOP; i++) {
    struct sigaltstack old;
    auto r = sys_sigaltstack(nullptr, &old);
    if (r != 0) {
      put_str("bench: sigaltstack failed\n");
      return;
    }
  }
  bench_syscall_ticks = rt_ticks() - t0;

  t0 = rt_ticks();
  for (unsigned long i = 0; i < BENCH_YIELD; i++) {
    if (rt_fiber_wake_one(&bench_empty_q)) {
      put_str("bench: woke someone\n");
      return;
    }
  }
  bench_wake_ticks = rt_ticks() - t0;

  t0 = rt_ticks();
  for (unsigned long i = 0; i < BENCH_SPAWN; i++) {
    if (rt_fiber_spawn(bench_child, nullptr) != 0) {
      put_str("bench: spawn failed\n");
      return;
    }
    rt_fiber_yield();
  }
  bench_spawn_ticks = rt_ticks() - t0;
}

static void rt_bench(struct rt_scheduler *s) {
  put_str("bench: counter ");
  put_dec(rt_tick_hz());
  put_str(" Hz\n");

  if (rt_scheduler_spawn(s, bench_fiber, nullptr) != 0) {
    put_str("bench: spawn failed\n");
    return;
  }
  rt_scheduler_run(s);

  bench_report("bench: yield round trip   ", bench_yield_ticks, BENCH_YIELD);
  bench_report("bench: ring nop round trip", bench_nop_ticks, BENCH_NOP);
  bench_report("bench: rt_fiber_current   ", bench_current_ticks,
               BENCH_CURRENT);
  bench_report("bench: rt_ticks itself    ", bench_clock_ticks, BENCH_CURRENT);
  bench_report("bench: wake (switch pair) ", bench_wake_ticks, BENCH_YIELD);
  bench_report("bench: bare syscall       ", bench_syscall_ticks, BENCH_NOP);
  bench_report("bench: spawn+exit+yield   ", bench_spawn_ticks, BENCH_SPAWN);

  if (bench_spawned != BENCH_SPAWN) {
    put_str("bench: spawn count mismatch\n");
  }

  bench_batch(s, 1);
  bench_batch(s, 4);
  bench_batch(s, 16);
  bench_batch(s, 30);
}

static constexpr int RT_AF_INET = 2;
static constexpr int RT_SOCK_STREAM = 1;
static constexpr int RT_IPPROTO_TCP = 6;

static constexpr unsigned RT_ECHO_PORT = 8080;
static constexpr int RT_ECHO_BACKLOG = 64;

static volatile bool echo_connected;
static unsigned echo_conn_id;

static void put_res(const char *what, int res) {
  put_str(what);
  if (res < 0) {
    put('-');
    put_dec((unsigned long)-(long)res);
  } else {
    put_dec((unsigned long)res);
  }
  put('\n');
}

static void echo_close(int fd) {
  auto res = rt_close(fd);
  if (res != 0) {
    put_res("echo: close ", res);
  }
}

static void conn_fiber(void *arg) {
  const int fd = (int)(intptr_t)arg;
  const unsigned id = ++echo_conn_id;

  put_str("echo: open ");
  put_dec(id);
  put('\n');

  char buf[512];
  bool open = true;

  while (open) {
    auto n = rt_recv(fd, buf, (unsigned)sizeof buf);
    if (n <= 0) {
      if (n < 0) {
        put_res("echo: recv ", n);
      }
      break;
    }

    unsigned sent = 0;
    while (sent < (unsigned)n) {
      auto w = rt_send(fd, buf + sent, (unsigned)n - sent);
      if (w <= 0) {
        put_res("echo: send ", w);
        open = false;
        break;
      }
      sent += (unsigned)w;
    }
  }

  echo_close(fd);

  put_str("echo: done ");
  put_dec(id);
  put('\n');
}

static void accept_fiber([[maybe_unused]] void *arg) {
  auto fd = rt_socket(RT_AF_INET, RT_SOCK_STREAM, RT_IPPROTO_TCP);
  if (fd < 0) {
    put_res("echo: socket ", fd);
    return;
  }

  struct sockaddr_in addr = {
      .sin_family = RT_AF_INET,
      .sin_port = __constant_htons(RT_ECHO_PORT),
      .sin_addr = {.s_addr = INADDR_ANY},
  };

  auto res = rt_bind(fd, &addr, (unsigned)sizeof addr);
  if (res != 0) {
    put_res("echo: bind ", res);
    echo_close(fd);
    return;
  }

  res = rt_listen(fd, RT_ECHO_BACKLOG);
  if (res != 0) {
    put_res("echo: listen ", res);
    echo_close(fd);
    return;
  }

  put_str("echo: listening\n");

  for (;;) {
    auto client = rt_accept(fd);
    if (client < 0) {
      put_res("echo: accept ", client);
      break;
    }

    echo_connected = true;

    auto spawned = rt_fiber_spawn(conn_fiber, (void *)(intptr_t)client);
    if (spawned != 0) {
      put_str("echo: no free fiber\n");
      echo_close(client);
    }
  }

  echo_close(fd);
}

static struct rt_fiber_queue park_q;

static void parker_fiber([[maybe_unused]] void *arg) {
  rt_fiber_park(&park_q);
  put_str("park/wake ok\n");
}

static void waker_fiber([[maybe_unused]] void *arg) {
  if (!rt_fiber_wake_one(&park_q)) {
    put_str("park: nobody parked\n");
  }
}

static void rt009_park(struct rt_scheduler *s) {
  auto parker = rt_scheduler_spawn(s, parker_fiber, nullptr);
  auto waker = rt_scheduler_spawn(s, waker_fiber, nullptr);
  if (parker != 0 || waker != 0) {
    put_str("park: spawn failed\n");
    return;
  }
  rt_scheduler_run(s);
}

static void yield_fiber([[maybe_unused]] void *arg) {
  while (!echo_connected) {
    rt_fiber_yield();
  }
}

static void rt006_demo(struct rt_scheduler *s) {

  static char msg_a[] = "hello a\n";
  static char msg_b[] = "hello b\n";

  auto a = rt_scheduler_spawn(s, hello_fiber, msg_a);
  auto b = rt_scheduler_spawn(s, hello_fiber, msg_b);
  if (a != 0 || b != 0) {
    put_str("demo: spawn failed\n");
    return;
  }

  rt_scheduler_run(s);
}

static void rt010_echo_server(struct rt_scheduler *s) {
  auto acceptor = rt_scheduler_spawn(s, accept_fiber, nullptr);
  auto yielder = rt_scheduler_spawn(s, yield_fiber, nullptr);
  if (acceptor != 0 || yielder != 0) {
    put_str("echo: spawn failed\n");
    return;
  }

  rt_scheduler_run(s);
}

static constexpr size_t RT_FIBER_MAX = 32;
static struct rt_fiber fibers[RT_FIBER_MAX];

[[noreturn]] void rt_main(void *stack);

[[noreturn]] void rt_main(void *stack) {

  rt_crash_install();

  rt_auxv_init(stack);

  struct rt_scheduler sched;
  auto init = rt_scheduler_init(&sched, 128);
  if (sys_failed(init)) {
    put_str("scheduler init failed: ");
    put_dec((unsigned long)-init);
    put('\n');
    for (;;) {
      __asm__ volatile("wfe");
    }
  }

  rt_scheduler_provide(&sched, fibers, RT_FIBER_MAX);

  static volatile bool bench = false;
  if (bench) {
    rt_bench(&sched);
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
  rt010_echo_server(&sched);

  for (;;) {
    __asm__ volatile("wfe");
  }
}
