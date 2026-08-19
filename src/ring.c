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

#include <stdalign.h>

#include <asm/errno.h>  /* EOPNOTSUPP */
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
constexpr unsigned RT_RING_FLAGS = IORING_SETUP_SINGLE_ISSUER |
                                   IORING_SETUP_DEFER_TASKRUN |
                                   IORING_SETUP_NO_SQARRAY;

int rt_ring_probe(struct io_uring_query_opcode *out) {
  *out = (struct io_uring_query_opcode){};

  struct io_uring_query_hdr hdr = {
      .query_op = IO_URING_QUERY_OPCODES,
      .query_data = (__u64)(uintptr_t)out,
      .size = (__u32)sizeof *out,
  };

  int r = sys_io_uring_register(-1, IORING_REGISTER_QUERY, &hdr, 0);
  if (sys_failed(r)) {
    return r;
  }

  return hdr.result;
}

int rt_ring_setup(struct rt_ring *r, unsigned entries) {
  /* a) Create. The designated initializer also zeroes resv[3], which the
   *    kernel checks. */
  struct io_uring_params params = {.flags = RT_RING_FLAGS};
  int ret = sys_io_uring_setup(entries, &params);
  if (sys_failed(ret)) {
    return ret;
  }
  r->fd = ret;

  /* b) Refuse. Everything below assumes the SQ and CQ rings share one
   *    mapping. Unconditional on this kernel (io_uring.c:3058), so this is a
   *    tripwire for the kernel moving, not a branch we expect to take. The fd
   *    leaks; nothing else has been acquired yet. */
  if (!(params.features & IORING_FEAT_SINGLE_MMAP)) {
    return -EOPNOTSUPP;
  }

  /* c) Map. Both MAP_SHARED — this memory is shared with the kernel, and
   *    MAP_PRIVATE would hand us a copy it never sees. The offsets are
   *    positions in the ring fd's address space, which is how one fd yields
   *    two mappings.
   *
   *    The ring length must cover the furthest thing inside it, which is the
   *    CQE array. Not sq_off.array: under NO_SQARRAY the kernel never assigns
   *    it (io_uring.c:2941), so it is zero and that arithmetic yields a
   *    mapping far too small — mmap succeeds and the fault arrives later, on a
   *    CQE read, looking like a missing barrier.
   *
   *    A failure past this point leaks the fd and possibly the first mapping.
   *    There is no close() wrapper yet and IORING_OP_CLOSE needs a working
   *    ring, so for a ring created once at boot this is accepted rather than
   *    handled. */
  r->ring_len =
      params.cq_off.cqes + params.cq_entries * sizeof(struct io_uring_cqe);

  long m = sys_mmap(nullptr, r->ring_len, PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_POPULATE, r->fd, IORING_OFF_SQ_RING);
  if (sys_failed(m)) {
    return (int)m;
  }
  r->ring = (unsigned char *)(uintptr_t)m;

  r->sqes_len = params.sq_entries * sizeof(struct io_uring_sqe);

  m = sys_mmap(nullptr, r->sqes_len, PROT_READ | PROT_WRITE,
               MAP_SHARED | MAP_POPULATE, r->fd, IORING_OFF_SQES);
  if (sys_failed(m)) {
    return (int)m;
  }
  r->sqes = (struct io_uring_sqe *)(uintptr_t)m;

  /* d) Derive. Two kinds of offset: head and tail change, so they stay
   *    pointers; the masks never change after setup, so they are read once and
   *    cached as values.
   *
   *    __builtin_assume_aligned rather than a cast through void *: it states
   *    the claim instead of hiding the cast from -Wcast-align, and
   *    -fsanitize=alignment turns it into a runtime check. mmap returns
   *    page-aligned memory and the kernel lays these fields out naturally
   *    aligned within it, so the claim holds — but it is now checked rather
   *    than asserted. (Only locally until the container build carries UBSan;
   *    that is RT-007's __ubsan_handle_* work.) */
  r->sq_head = (_Atomic unsigned *)__builtin_assume_aligned(
      r->ring + params.sq_off.head, alignof(_Atomic unsigned));
  r->sq_tail = (_Atomic unsigned *)__builtin_assume_aligned(
      r->ring + params.sq_off.tail, alignof(_Atomic unsigned));
  r->cq_head = (_Atomic unsigned *)__builtin_assume_aligned(
      r->ring + params.cq_off.head, alignof(_Atomic unsigned));
  r->cq_tail = (_Atomic unsigned *)__builtin_assume_aligned(
      r->ring + params.cq_off.tail, alignof(_Atomic unsigned));
  r->cqes = (struct io_uring_cqe *)__builtin_assume_aligned(
      r->ring + params.cq_off.cqes, alignof(struct io_uring_cqe));

  r->sq_mask = *(unsigned *)__builtin_assume_aligned(
      r->ring + params.sq_off.ring_mask, alignof(unsigned));
  r->cq_mask = *(unsigned *)__builtin_assume_aligned(
      r->ring + params.cq_off.ring_mask, alignof(unsigned));

  r->features = params.features;
  return 0;
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
