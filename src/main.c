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
 *     listen ok
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
#include "ring.h"
#include "sched.h"
#include "syscall.h"
#include "task.h"

/* Console output for the failure paths and the regression chain — the
 * purity registry's charter. Task bodies use rt_write instead; the paths
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

static void abc_task([[maybe_unused]] void *arg) {
  put('A');
  rt_yield();
  put('B');
  rt_yield();
  put('C');
}

/* RT-004 acceptance: the cooperative round trip. Expected line: "1A2B3C" —
 * the interleaving proves the resume/yield alternation, not just that both
 * sides ran. */
static void rt004_selftest(void) {
  struct rt_task t;
  rt_task_create(&t, abc_task, nullptr);

  int count = 0;
  while (t.state != RT_DEAD) {
    count++;
    put((char)('0' + count));
    rt_sched_resume(&t);
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

/* The milestone demonstration: NOP, then hello through the ring — the task
 * suspends twice and survives both. raw_write is forbidden in task bodies;
 * the put_* here are failure reporting only. */
static void hello_task(void *arg) {
  const char *msg = arg;

  auto res = rt_nop();
  if (res != 0) {
    put_str("task: nop res ");
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
    put_str("task: write res ");
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

/* Milestone 2's first checkpoint: prove SOCKET -> BIND -> LISTEN -> CLOSE
 * through the scheduler's ring. After SOCKET succeeds, every path reaches
 * CLOSE before the task returns. */
static void socket_task([[maybe_unused]] void *arg) {
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

  if (bind_res != 0 || listen_res != 0) {
    return;
  }

  static constexpr char success_msg[] = "listen ok\n";
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

static void rt006_demo(void) {
  auto ret = rt_sched_init(8);
  if (sys_failed(ret)) {
    put_str("sched init failed: ");
    put_dec((unsigned long)-ret);
    put('\n');
    return;
  }

  /* Two tasks with distinct messages: one task cannot distinguish "resumes
   * the right task" from "resumes the only task", and identical lines would
   * prove count, not identity. */
  static char msg_a[] = "hello a\n";
  static char msg_b[] = "hello b\n";
  struct rt_task a;
  struct rt_task b;
  rt_task_create(&a, hello_task, msg_a);
  rt_task_create(&b, hello_task, msg_b);

  struct rt_task *tasks[] = {&a, &b};
  rt_sched_run(tasks, 2);

  /* Run the first milestone-2 probe separately, but on the same ring. Besides
   * keeping the console order exact, one task means SQ capacity cannot be the
   * source of a SOCKET/BIND/LISTEN/CLOSE failure in this checkpoint. */
  struct rt_task socket;
  rt_task_create(&socket, socket_task, nullptr);
  struct rt_task *socket_tasks[] = {&socket};
  rt_sched_run(socket_tasks, 1);
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

  /* The regression chain, compiled in but quiet: the happy path wants the
   * three lines documented above and nothing else, and AGENTS.md wants the old
   * acceptance checks runnable when shared code changes. volatile keeps the
   * chain alive in the binary; flip to true to re-run it. */
  static volatile bool verbose = false;
  if (verbose) {
    static constexpr char banner[] = "rt: alive\n";
    raw_write(1, banner, sizeof banner - 1);
    rt004_selftest();
    rt005_probe();
    rt005_setup_failure();
    rt005_nop();
  }
  rt006_demo();

  for (;;) {
    __asm__ volatile("wfe");
  }
}
