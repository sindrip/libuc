/*
 * RT-005 driver: one ring, one NOP, one CQE.
 *
 * NOP is deliberate. It has no fd and touches no I/O path, so anything that
 * goes wrong here is submission or completion mechanics and nothing else.
 *
 * Expected console:
 *
 *     rt: alive
 *     1A2B3C
 *     ops <n> reg <n> feat <n>
 *     setup rejects bogus flags: <errno>
 *     nop ok
 *
 * The values are exact, not approximate — the kernel is pinned, so
 * nr_request_opcodes and the feature mask are constants for this tree. Record
 * what you observe in the ticket; a change in them later means the kernel
 * moved, which is worth knowing loudly.
 *
 * Every line is a proof:
 *   'rt: alive'   RT-003 substrate: start.S, syscall.h, raw_write
 *   '1A2B3C'      RT-004 still works. Not sentiment — this ticket edits
 *                 syscall.h, which task.c includes, and AGENTS.md requires
 *                 re-running the checks of every ticket that depends on code
 *                 you touched. Six characters is the whole cost.
 *   'ops ...'     io_uring_register reached the kernel on the ringless path,
 *                 so the query ABI and the hdr constraints are right
 *   'setup ...'   the failure path reports a decoded errno instead of hanging
 *                 or dying silently
 *   'nop ok'      setup, both mappings, SQE fill, tail publish, enter, and CQ
 *                 reap all agree
 *
 * Reading a failure:
 *
 *   nothing at all        substrate broken. Debug RT-003, not ring.c.
 *   stops after 1A2B3C    the probe faulted or trapped — a ring.c stub is
 *                         still a __builtin_trap(), or the query hdr was
 *                         rejected. A brk shows as exitcode=0x00000005.
 *   probe prints zeros    the syscall returned 0 but hdr.result did not; the
 *                         per-entry status was never checked.
 *   'nop ok' missing      reached the ring but the round trip failed: wrong
 *                         mapping size, wrong stride, or a missing barrier.
 *                         A too-small ring mapping faults on the CQE read.
 *   hangs after setup     enter is waiting for a completion that was never
 *                         submitted — the tail was published without release,
 *                         or to_submit was zero.
 */
#include "crash.h"
#include "ring.h"
#include "syscall.h"
#include "task.h"

/* Emit a single character. */
static void put(char c) { raw_write(1, &c, 1); }

/* Emit a NUL-terminated string in a single write.
 *
 * One syscall per string, not per character — the same argument that made
 * put_dec format into a buffer rather than call put twenty times.
 *
 * raw_write needs a length and a C string does not carry one, so find the
 * terminator first and let the distance be the length. That subtraction is a
 * ptrdiff_t, which is signed, so it needs the same explicit cast put_dec uses.
 *
 * Taking `const char *` rather than a length is what keeps this a plain
 * function: a length would have to come from sizeof at each call site, and
 * sizeof only sees the array inside a macro. It costs nothing — clang folds
 * the walk to a constant for literals, emitting the length directly.
 */
static void put_str(const char *s) {
  size_t n = 0;
  while (s[n]) {
    n++;
  }
  raw_write(1, s, n);
}

/* Print an unsigned value in decimal.
 *
 * Needed twice over: the probe has numbers to report, and the failure path has
 * to decode an -errno into something readable. Without it "reports a decoded
 * errno" degrades to "returns nonzero", which is not a diagnostic.
 *
 * Digits come out least-significant first, so something has to reverse them —
 * a small buffer filled backwards, or recursion. Either is fine; pick the one
 * you would rather read at 3am.
 *
 * The freestanding question worth asking: does dividing by 10 emit a call to
 * a runtime library that does not exist here? On this target, no — clang
 * strength-reduces a constant divisor into a multiply-high and msub, so no
 * division instruction and no __udivdi3. Confirmed, not assumed; on a 32-bit
 * target the same code links against compiler-rt.
 *
 * Decide what zero prints. A `while (v)` loop emits nothing for it, and
 * silence is the worst possible rendering of a value you wanted to inspect.
 */
static void put_dec(unsigned long v) {
  char buf[20];
  char *p = buf + sizeof buf;

  do {
    *--p = (char)('0' + v % 10);
    v /= 10;
  } while (v);

  raw_write(1, p, (size_t)(buf + sizeof buf - p));
}

/* The task body. */
static void abc_task(void *arg) {
  (void)arg;

  put('A');
  rt_yield();
  put('B');
  rt_yield();
  put('C');
}

/* The cooperative round trip, kept as a regression check. */
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

/* Probe the kernel and report what it can do.
 *
 * rt_ring_probe fills an io_uring_query_opcode; print nr_request_opcodes,
 * nr_register_opcodes and feature_flags. That is one syscall, and it converts
 * every future "why doesn't this opcode work" into a question with an answer
 * already on the console.
 *
 * Two of those fields are worth understanding rather than just printing.
 * nr_request_opcodes is IORING_OP_LAST (query.c:23), so it is a count and the
 * highest valid opcode is one less. feature_flags is the full mask the kernel
 * supports, which is not the same thing as params.features from setup — that
 * one describes the ring you actually created.
 *
 * IORING_FEAT_NO_IOWAIT (bit 17) is new on 7.2 and worth noticing in the
 * output. The ticket says log it, not act on it.
 */
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

/* Exercise the failure path once, on purpose.
 *
 * Call setup with a flag combination the kernel must reject and print the
 * decoded errno. SQPOLL alongside DEFER_TASKRUN is the honest choice: it fails
 * for a reason this design depends on, at io_uring.c:2815-2821, rather than
 * because a bit is nonsense.
 *
 * This is the sanctioned purity exception doing its job — a ring that failed
 * to exist cannot report its own failure, so this goes out through raw_write.
 * It is also the last chance to prove the reporting path works while you still
 * know the answer; after this, failures will be ones you did not predict.
 *
 * Do not leak the fd if it unexpectedly succeeds. There is no close() wrapper
 * yet, and IORING_OP_CLOSE cannot help before a ring exists.
 */
static void rt005_setup_failure(void) {
  /* Valid entries, so the flags are the only possible cause of failure. */
  struct io_uring_params params = {.flags = IORING_SETUP_SQPOLL |
                                            IORING_SETUP_DEFER_TASKRUN};
  int ret = sys_io_uring_setup(8, &params);
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

/* The NOP round trip.
 *
 * Set up a ring, take an SQE, make it a NOP with a sentinel user_data,
 * submit-and-wait for one completion, reap it.
 *
 * Pick a sentinel that cannot be confused with zero-filled memory or with a
 * pointer — if user_data comes back as 0 you want to know whether that means
 * "the kernel echoed our value" or "we read an SQE that was never written".
 *
 * Three things the acceptance criteria actually pin down, all of which can
 * fail independently:
 *   - enter returns 1, meaning the kernel consumed exactly the one SQE
 *   - the CQE's res is 0, meaning NOP itself succeeded
 *   - the CQE's user_data equals the sentinel, meaning you reaped *your*
 *     completion out of the slot you think you did
 *
 * Check all three before printing "nop ok". A reaping loop with the stride
 * wrong reads a plausible-looking CQE from the wrong offset, and only the
 * user_data comparison catches it.
 */
/* Top byte set: cannot be mistaken for zero-filled memory or for any
 * userspace pointer (aarch64 user VAs keep the high bits clear). */
static constexpr __u64 NOP_SENTINEL = 0xF00DFACEDEADBEEF;

static void rt005_nop(void) {
  struct rt_ring ring;

  int ret = rt_ring_setup(&ring, 8);
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

/* TODO(5) [RT-006]: the driver rewrite — the ticket's demonstration: tasks
 * that rt_nop() then rt_write() hello through the ring (raw_write becomes
 * forbidden in task bodies), two of them interleaving, verified by
 * temporarily stubbing raw_write to trap. Forces the acceptance decision:
 * the happy-path console shows `hello` and nothing else, so the RT-005
 * driver lines retire or gate when this lands — decide then, not before.
 */

/* The only caller is start.S, which passes the pre-alignment stack pointer in
 * x0 (arch/aarch64/start.S:44). Declared here because assembly cannot be
 * checked against a C signature — without this, nothing at all verifies the
 * two agree. */
[[noreturn]] void rt_main(void *stack);

[[noreturn]] void rt_main(void *stack) {
  (void)stack;

  /* First, before anything that can fault: a handler installed after the
   * crash reports nothing. */
  rt_crash_install();

  static const char banner[] = "rt: alive\n";
  raw_write(1, banner, sizeof banner - 1);

  rt004_selftest();
  rt005_probe();
  rt005_setup_failure();
  rt005_nop();

  for (;;) {
    __asm__ volatile("wfe");
  }
}
