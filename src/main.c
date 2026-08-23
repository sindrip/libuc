/*
 * The boot driver, PID 1. Order: crash handlers first (a handler installed
 * after the crash reports nothing), then the gated regression chain, then
 * the scheduler demonstration, then idle forever — rt_main never returns
 * (invariant 6: the kernel panics with "Attempted to kill init").
 *
 * The happy-path console is exactly
 *
 *     hello a
 *     hello b
 *     echo ok
 *
 * written by the kernel through IORING_OP_WRITE. With `verbose` flipped on,
 * the acceptance chain of the earlier tickets prints first — the expected
 * line is documented at each rt00N_* function, and the values are exact,
 * not approximate: the kernel is pinned, so a change means the kernel
 * moved, which is worth knowing loudly. AGENTS.md's testing discipline is
 * why the chain stays compiled in: there is no other regression net.
 *
 * Reading a chain failure: nothing at all means the substrate (start.S,
 * raw_write) is broken; stopping after 1A2B3C means the probe faulted (a
 * brk shows as exitcode=0x00000005); probe zeros mean hdr.result went
 * unchecked; a missing "nop ok" means the ring round trip failed — wrong
 * mapping size, wrong stride, or a missing barrier, and a too-small mapping
 * faults on the CQE read; a hang after setup means enter waited for
 * something never submitted (tail published without release, or to_submit
 * zero).
 */
#include <stddef.h>

#include <asm/byteorder.h>
#include <linux/in.h>

#include "crash.h"
#include "fiber.h"
#include "io.h"
#include "ring.h"
#include "scheduler.h"
#include "syscall.h"

/* Console output for the failure paths and the regression chain — the
 * purity registry's charter. Fiber bodies use rt_write instead; the paths
 * that need these cannot trust the ring, or predate it. */
static void put(char c) { raw_write(1, &c, 1); }

/* One write per string, not per character. raw_write needs a length and a C
 * string does not carry one, so find the terminator and let the distance be
 * the length; clang folds the walk to a constant for literals. */
static void put_str(const char *s) {
  size_t n = 0;
  while (s[n]) {
    n++;
  }
  raw_write(1, s, n);
}

/* Decimal, via a scratch buffer filled backwards — digits emerge
 * least-significant first — with do-while so zero prints "0" instead of
 * nothing. The % and / by a constant 10 compile to multiply-high, not a
 * libcall: freestanding has no __udivdi3 to link (verified; a 32-bit target
 * would need compiler-rt). */
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

/* RT-004 acceptance: the cooperative round trip. Expected line: "1A2B3C" —
 * the interleaving proves the resume/yield alternation, not just that both
 * sides ran.
 *
 * rt_fiber_create rather than rt_scheduler_spawn, and rt_scheduler_resume
 * rather than rt_scheduler_run: this drives the switch by hand, so the fiber
 * must stay off the ready queue and out of the live count. The scheduler is
 * still needed and always was — it is what a yielding fiber switches back
 * into; before it was a struct, that was an implicit global. */
static void rt004_selftest(struct rt_scheduler *s) {
  struct rt_fiber t;
  rt_fiber_create(&t, abc_fiber, nullptr);

  /* Loops on the request rather than the state: driving resume by hand means
   * no dispatch runs, and state is the scheduler's to assign at dispatch. The
   * request is what the fiber itself said, which is the honest thing to read
   * when standing in for the loop. */
  int count = 0;
  while (t.request.kind != RT_REQUEST_EXIT) {
    count++;
    put((char)('0' + count));
    rt_scheduler_resume(s, &t);
  }
  put('\n');
}

/* RT-005 acceptance: the ringless capability probe. Expected line:
 * "ops <n> reg <n> feat <n>".
 *
 * nr_request_opcodes is IORING_OP_LAST (query.c:23) — a count, so the
 * highest valid opcode is one less. feature_flags is the full mask the
 * kernel supports, which is not the same thing as params.features from
 * setup: that one describes the ring actually created. */
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

/* RT-005 acceptance: the failure path, exercised on purpose while the
 * answer is known. Expected line: "setup rejects bogus flags: <errno>".
 *
 * SQPOLL alongside DEFER_TASKRUN is the honest choice of bogus flags: it
 * fails for a reason this design depends on (io_uring.c:2815-2821), rather
 * than because a bit is nonsense. */
static void rt005_setup_failure(void) {
  /* Valid entries, so the flags are the only possible cause of failure. */
  struct io_uring_params params = {.flags = IORING_SETUP_SQPOLL |
                                            IORING_SETUP_DEFER_TASKRUN};
  auto ret = sys_io_uring_setup(8, &params);
  if (!sys_failed(ret)) {
    /* The kernel moved. The fd leaks — closing needs an opcode and a working
     * ring (invariant 1) — so report loudly and carry on. */
    put_str("setup accepted bogus flags\n");
    return;
  }

  put_str("setup rejects bogus flags: ");
  put_dec((unsigned long)-ret);
  put('\n');
}

/* Top byte set: cannot be mistaken for zero-filled memory or for any
 * userspace pointer (aarch64 user VAs keep the high bits clear). */
static constexpr __u64 NOP_SENTINEL = 0xF00D'FACE'DEAD'BEEF;

/* RT-005 acceptance: one ring, one NOP, one CQE. Expected line: "nop ok",
 * printed only after three independent checks: enter consumed exactly one
 * SQE, the CQE's res is 0, and its user_data echoes the sentinel — the last
 * is what catches a wrong-stride reap reading a plausible-looking CQE from
 * the wrong offset. */
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

/* The milestone demonstration: NOP, then hello through the ring — the fiber
 * suspends twice and survives both. raw_write is forbidden in fiber bodies;
 * the put_* here are failure reporting only. */
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

/* Temporary milestone-local names. A real libuc <sys/socket.h> eventually
 * owns these, but that header surface is not part of this first socket proof.
 */
static constexpr int RT_AF_INET = 2;
static constexpr int RT_SOCK_STREAM = 1;
static constexpr int RT_IPPROTO_TCP = 6;

/* Milestone 2's first checkpoint: prove SOCKET -> BIND -> LISTEN -> ACCEPT ->
 * RECV -> SEND -> CLOSE through the scheduler's ring. After SOCKET succeeds,
 * every path reaches CLOSE before the fiber returns. */
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

/* PARK/WAKE acceptance. Expected line: "park/wake ok".
 *
 * The ordering is the proof, not just the arrival: parker runs first (spawn
 * order is queue order), finds nothing to do and parks, at which point it is
 * on a queue the scheduler does not own and no kernel event exists that could
 * move it. waker then pops it and asks the scheduler to make it runnable. If
 * WAKE did not work the scheduler would panic with the deadlock report rather
 * than hang — ready empty, nothing in flight, one fiber still alive. */
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

/* Liveness, checked by the absence of a hang rather than by a line of its own.
 *
 * Under DEFER_TASKRUN a completion reaches the shared CQ only inside
 * io_uring_enter — io_cqring_wait runs the deferred work (wait.c:189-198) — so
 * a loop that skips the enter because some fiber is runnable never reaps. This
 * fiber keeps the ready queue non-empty for exactly as long as echo_once is
 * blocked on ACCEPT, which is that condition. It prints nothing: the check is
 * that "echo ok" appears at all. */
static volatile bool echo_finished;

static void yield_fiber([[maybe_unused]] void *arg) {
  while (!echo_finished) {
    rt_fiber_yield();
  }
}

/* The flag is set here rather than at the end of echo_once, so that every one
 * of its early returns releases the yielding fiber too — a failure should
 * report and exit, not wedge the VM. */
static void socket_fiber([[maybe_unused]] void *arg) {
  echo_once();
  echo_finished = true;
}

static void rt006_demo(struct rt_scheduler *s) {
  /* Two fibers with distinct messages: one fiber cannot distinguish "resumes
   * the right fiber" from "resumes the only fiber", and identical lines would
   * prove count, not identity. */
  static char msg_a[] = "hello a\n";
  static char msg_b[] = "hello b\n";
  struct rt_fiber a;
  struct rt_fiber b;
  rt_scheduler_spawn(s, &a, hello_fiber, msg_a);
  rt_scheduler_spawn(s, &b, hello_fiber, msg_b);

  rt_scheduler_run(s);

  /* Run the first milestone-2 probe separately, but on the same scheduler.
   * Besides keeping the console order exact, one fiber means SQ capacity
   * cannot be the source of a SOCKET/BIND/LISTEN/ACCEPT/RECV/SEND/CLOSE
   * failure here.
   *
   * Two phases on one scheduler is why rt_scheduler_run returns at all: the
   * loop stops when its last fiber dies, and the spawn below refills a
   * scheduler whose queue is empty and whose in-flight count is zero. The
   * destination loop does not return (.scratch/scheduler.md). */
  struct rt_fiber socket;
  struct rt_fiber yielder;
  rt_scheduler_spawn(s, &socket, socket_fiber, nullptr);
  rt_scheduler_spawn(s, &yielder, yield_fiber, nullptr);
  rt_scheduler_run(s);
}

/* The only caller is start.S, which passes the pre-alignment stack pointer in
 * x0 (arch/aarch64/start.S). Declared here because assembly cannot be
 * checked against a C signature — without this, nothing at all verifies the
 * two agree. */
[[noreturn]] void rt_main(void *stack);

[[noreturn]] void rt_main([[maybe_unused]] void *stack) {
  /* First, before anything that can fault: a handler installed after the
   * crash reports nothing. */
  rt_crash_install();

  /* The scheduler is not created, it is *become*: this thread fills in
   * scheduler 0's state and from here on it is the scheduler, running on the
   * kernel-supplied stack. Every later scheduler follows the same sequence on
   * its own thread — nothing manufactures one on another thread's behalf
   * (.scratch/scheduler.md). */
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

  /* The regression chain, compiled in but quiet: the happy path wants the
   * three lines documented above and nothing else, and AGENTS.md wants the old
   * acceptance checks runnable when shared code changes. volatile keeps the
   * chain alive in the binary; flip to true to re-run it. */
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
