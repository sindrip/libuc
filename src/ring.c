/*
 * Ring mechanics. The requirements and traps for each function are in ring.h,
 * next to its declaration.
 *
 * Every stub below traps. That is deliberate: a stub returning 0 or -1 would
 * let the driver run and fail somewhere else, and __builtin_trap() compiles to
 * a single brk that stops at a known address instead. Replacing a trap with a
 * body is the work of this ticket.
 */
#include "ring.h"

#include <linux/mman.h> /* MAP_SHARED, MAP_POPULATE, PROT_* */

#include "syscall.h"

/* TODO(1) [RT-005]: the setup flags for every ring this runtime creates.
 *
 *   SINGLE_ISSUER  one thread owns this ring for its whole life. True by
 *                  construction in a shared-nothing design (invariant 3), and
 *                  DEFER_TASKRUN hard-requires it.
 *   DEFER_TASKRUN  completion work runs when we ask for it by entering with
 *                  GETEVENTS, not at arbitrary syscall boundaries. That is
 *                  what makes a cooperative scheduler's timing its own.
 *   NO_SQARRAY     no array[] indirection to maintain; the SQ head and tail
 *                  index the SQEs directly (io_uring.c:1990-1996). Simpler,
 *                  not merely faster.
 *
 * Never SQPOLL — rejected outright alongside DEFER_TASKRUN, COOP_TASKRUN and
 * TASKRUN_FLAG at io_uring.c:2815-2821.
 *
 * Zero is not a placeholder that fails loudly: a ring with no flags is a valid
 * ring, so nothing will tell you this was left alone except a scheduler whose
 * completions run at times it did not choose. It is safe only while
 * rt_ring_setup below is still a trap.
 *
 * [[maybe_unused]] until rt_ring_setup reads it — an unused constexpr is an
 * error under -Werror. Delete the attribute when you use it, so a constant
 * that later becomes orphaned is still reported.
 */
[[maybe_unused]] constexpr unsigned RT_RING_FLAGS = IORING_SETUP_SINGLE_ISSUER |
                                                    IORING_SETUP_DEFER_TASKRUN |
                                                    IORING_SETUP_NO_SQARRAY;

int rt_ring_probe(struct io_uring_query_opcode *out) {
  *out = (struct io_uring_query_opcode){};

  struct io_uring_query_hdr hdr = {
      .query_op = IO_URING_QUERY_OPCODES,
      .query_data = (__u64)(uintptr_t)out,
      .size = (__u32)sizeof *out,
  };

  long r = sys_io_uring_register(-1, IORING_REGISTER_QUERY, &hdr, 0);
  if (sys_failed(r)) {
    return (int)r;
  }

  return hdr.result;
}

int rt_ring_setup(struct rt_ring *r, unsigned entries) {
  (void)r;
  (void)entries;
  __builtin_trap();
}

struct io_uring_sqe *rt_ring_sqe(struct rt_ring *r) {
  (void)r;
  __builtin_trap();
}

long rt_ring_submit_and_wait(struct rt_ring *r, unsigned to_submit,
                             unsigned min_complete) {
  (void)r;
  (void)to_submit;
  (void)min_complete;
  __builtin_trap();
}

bool rt_ring_reap(struct rt_ring *r, struct io_uring_cqe *out) {
  (void)r;
  (void)out;
  __builtin_trap();
}
