/*
 * Ring mechanics, hand-rolled (invariant 5). The contract and traps for each
 * function live in ring.h, next to its declaration; the comments here carry
 * what only the bodies can — the ordering handshakes, and where each one
 * pairs with the kernel's side of the protocol.
 */

#include "ring.h"

#include <stdatomic.h>

#include <asm/errno.h>  /* EOPNOTSUPP */
#include <linux/mman.h> /* MAP_SHARED, MAP_POPULATE, PROT_* */

#include "syscall.h"

/* the setup flags for every ring this runtime creates.
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
 */
constexpr unsigned RT_RING_FLAGS = IORING_SETUP_SINGLE_ISSUER |
                                   IORING_SETUP_DEFER_TASKRUN |
                                   IORING_SETUP_NO_SQARRAY;

/* Ring stride comes from the setup flags, not from the types: SQE and CQE
 * both end in a flexible array member, so sizeof stays 64 and 16 no matter
 * which flags the ring was created with — under SQE128 or CQE32 the real
 * stride doubles, and every sizeof-based computation in this file silently
 * walks the ring at half step, reading garbage from every other entry.
 * Asserting the sizes therefore proves nothing; assert the real assumption —
 * that RT_RING_FLAGS contains neither stride-changing flag — so the day one
 * is added, the build breaks here instead of the reap loop corrupting
 * silently. */
static_assert(
    (RT_RING_FLAGS & (IORING_SETUP_SQE128 | IORING_SETUP_CQE32)) == 0,
    "stride arithmetic assumes 64-byte SQEs and 16-byte CQEs");

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
  /* Create. The designated initializer also zeroes resv[3], which the
   * kernel checks. */
  struct io_uring_params params = {.flags = RT_RING_FLAGS};
  int ret = sys_io_uring_setup(entries, &params);
  if (sys_failed(ret)) {
    return ret;
  }
  r->fd = ret;

  /* Refuse. Everything below assumes the SQ and CQ rings share one mapping.
   * Unconditional on this kernel (io_uring.c:3058), so this is a tripwire
   * for the kernel moving, not a branch we expect to take. The fd leaks;
   * nothing else has been acquired yet. */
  if (!(params.features & IORING_FEAT_SINGLE_MMAP)) {
    return -EOPNOTSUPP;
  }

  /* Map. Both MAP_SHARED — this memory is shared with the kernel, and
   * MAP_PRIVATE would hand us a copy it never sees. The offsets are
   * positions in the ring fd's address space, which is how one fd yields
   * two mappings.
   *
   * The ring length must cover the furthest thing inside it, which is the
   * CQE array. Not sq_off.array: under NO_SQARRAY the kernel never assigns
   * it (io_uring.c:2941), so it is zero and that arithmetic yields a
   * mapping far too small — mmap succeeds and the fault arrives later, on a
   * CQE read, looking like a missing barrier.
   *
   * A failure past this point leaks the fd and possibly the first mapping.
   * There is no close() wrapper and IORING_OP_CLOSE needs a working ring,
   * so for a ring created once at boot this is accepted rather than
   * handled. */
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

  /* Derive. Two kinds of offset: head and tail change, so they stay
   * pointers; the masks never change after setup, so they are read once and
   * cached as values.
   *
   * __builtin_assume_aligned rather than a cast through void *: it states
   * the claim instead of hiding the cast from -Wcast-align, and
   * -fsanitize=alignment turns it into a runtime check. mmap returns
   * page-aligned memory and the kernel lays these fields out naturally
   * aligned within it, so the claim holds — but it is checked rather than
   * asserted. */
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

  /* The private cursor starts equal to the shared tail it runs ahead of —
   * read, not zeroed, because "equal" is the invariant and 0 the coincidence.
   * Relaxed: the ring is brand new; there is nothing to order against. */
  r->cached_sq_tail = atomic_load_explicit(r->sq_tail, memory_order_relaxed);

  r->features = params.features;
  return 0;
}

struct io_uring_sqe *rt_ring_sqe(struct rt_ring *r) {
  /* Fullness. Full when cached_sq_tail - head == sq_mask + 1. Both counters
   * are free-running — they wrap through UINT_MAX and are masked only at
   * the moment of indexing — and that is exactly what makes the unsigned
   * subtraction correct across the wrap; masking before subtracting
   * destroys it. The tail side is the private cursor, a plain read: the
   * shared tail is not consulted here, since it lags by the staged slots
   * and testing it would hand a staged slot out twice. The head load is
   * acquire: the kernel advancing sq_head is its statement that it has
   * finished reading the slots below, and the writes into the reclaimed
   * slot must be ordered after observing it. (On this kernel the SQ is only
   * read inside our own enter call, so a plain load happens to work — write
   * the discipline, not the coincidence.)
   *
   * The nullptr return is the backpressure point: the scheduler's ring ops
   * surface it as -EAGAIN rather than suspending. */
  auto pending = r->cached_sq_tail -
                 atomic_load_explicit(r->sq_head, memory_order_acquire);

  if (pending == r->sq_mask + 1) {
    return nullptr;
  }

  /* The slot. cached_sq_tail & sq_mask indexes sqes directly — NO_SQARRAY
   * deleted the array[] indirection, mirroring the kernel's own io_get_sqe
   * (io_uring.c:1990-1996). */
  struct io_uring_sqe *sqe = &r->sqes[r->cached_sq_tail & r->sq_mask];

  /* Clear it. The slot still holds the whole previous request; a caller who
   * fills in three fields would inherit a stale buffer pointer, flags and
   * offset from whatever lived here before — a bug that only appears on the
   * second lap of the ring. */
  *sqe = (struct io_uring_sqe){};

  /* Advance the private cursor only. Nothing is published: the SQE is still
   * unfilled, and announcing it is submit's release store. */
  r->cached_sq_tail++;
  return sqe;
}

int rt_ring_submit_and_wait(struct rt_ring *r, unsigned to_submit,
                            unsigned min_complete) {
  /* Publish. The one release store the staging design converges on: every
   * write into the staged SQEs must be visible before the tail that exposes
   * them, and this store pairs with the kernel's acquire load of the tail
   * (io_uring.h:473-474). After it the rest-state invariant holds again:
   * *sq_tail == cached_sq_tail. */
  atomic_store_explicit(r->sq_tail, r->cached_sq_tail, memory_order_release);

  /* Enter. GETEVENTS unconditionally — under DEFER_TASKRUN it is what runs
   * the deferred completion work at all (io_uring.c:2659); min_complete then
   * makes the same call wait (io_cqring_wait, io_uring.c:2685). Consumption
   * is capped at what was published (io_uring.h:474-475), and if it falls
   * short of to_submit the kernel skips the wait and returns the count
   * (io_uring.c:2647), so to_submit should be exactly what was staged. */
  return sys_io_uring_enter(r->fd, to_submit, min_complete,
                            IORING_ENTER_GETEVENTS, nullptr, 0);
}

bool rt_ring_reap(struct rt_ring *r, struct io_uring_cqe *out) {
  /* Empty when head == tail — free-running counters again, but only equality
   * matters here: fullness is the kernel's problem (overflow handling), not
   * the consumer's. The tail is acquire, pairing with the kernel's release
   * in io_commit_cqring ("order cqe stores with ring update", io_uring.h:416):
   * the CQE must not be read before the tail that announced it. Our own head
   * is relaxed; this thread is its only writer. */
  unsigned head = atomic_load_explicit(r->cq_head, memory_order_relaxed);
  unsigned tail = atomic_load_explicit(r->cq_tail, memory_order_acquire);
  if (head == tail) {
    return false;
  }

  /* Copy out, never point in: the moment the head advances, this slot is the
   * kernel's to overwrite, and a caller holding a pointer into it has a
   * use-after-free with no allocator involved. */
  *out = r->cqes[head & r->cq_mask];

  /* Release: the slot must not be seen as free until the copy above is done.
   * The kernel sizes its room to write new CQEs from this counter
   * (io_uring.c:698). No private cursor on this side — unlike the SQ there
   * is no window between claiming and consuming; the copy is complete before
   * the head that frees the slot. */
  atomic_store_explicit(r->cq_head, head + 1, memory_order_release);
  return true;
}
