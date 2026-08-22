/*
 * One io_uring, set up by hand. No liburing (invariant 5).
 *
 * Every struct that crosses the kernel boundary comes from the pinned uapi
 * headers and is never retyped (invariant 4). What this file adds is the
 * bookkeeping the kernel does not describe: where each mapping landed, and
 * which pointers into it mean what.
 */
#ifndef RT_RING_H
#define RT_RING_H

#include <stddef.h>

#include <linux/io_uring.h>
#include <linux/io_uring/query.h>

struct rt_ring {
  int fd;

  /* SQ and CQ rings share one mapping under SINGLE_MMAP; SQEs are separate. */
  unsigned char *ring;
  size_t ring_len;
  struct io_uring_sqe *sqes;
  size_t sqes_len;

  /* Shared with the kernel: every access needs acquire or release ordering. */
  _Atomic unsigned *sq_head;
  _Atomic unsigned *sq_tail;
  _Atomic unsigned *cq_head;
  _Atomic unsigned *cq_tail;

  struct io_uring_cqe *cqes;

  /* The true SQ tail, private to the owning thread — hence not _Atomic. Runs
   * ahead of *sq_tail by the staged slots until submit publishes it; the
   * mirror of the kernel's own cached_sq_head (io_uring_types.h:353). */
  unsigned cached_sq_tail;

  /* Cached values, not pointers: unlike head and tail these never change.
   * Entry counts are mask + 1, so they are not stored. */
  unsigned sq_mask;
  unsigned cq_mask;

  /* Unrecoverable after setup returns, which is why it is kept. */
  unsigned features;
};

/* The five operations, implemented in ring.c.
 *
 * Conventions, applied to all of them: an -errno in sys_failed()'s range comes
 * straight back to the caller rather than being collapsed into a bool, because
 * "setup failed" and "setup failed with -EPERM" are different debugging
 * sessions and there is no strace here to recover the difference. And all five
 * are [[nodiscard]], syscall.h's convention: every return is an errno, a
 * backpressure signal, or a CQE, and ignoring any of them is a bug.
 */

/* Capability probe. Fills `out`; returns 0, or -errno.
 *
 * io_uring_register with fd == -1 takes the "blind" path that needs no ring
 * (register.c:1031), which is the whole point — this runs before setup, so a
 * failure later can be read against what the kernel said it supports.
 *
 * The argument is a chain of io_uring_query_hdr, not the result struct: each
 * header names an op, points at its own result buffer, and links to the next.
 * Four constraints from query.c, none of which produce a useful error message
 * when violated:
 *
 *   - nr_args must be 0 (query.c:133), unlike every other register opcode
 *   - hdr.__resv must be zero, hdr.result must be zero on input, and hdr.size
 *     must be non-zero (query.c:85)
 *   - the kernel copies *from* your result buffer before writing it
 *     (query.c:88), so it must be zeroed, not merely allocated
 *   - next_entry == 0 terminates the chain (query.c:136)
 *
 * And the part that bites: the syscall returning 0 does not mean the query
 * succeeded. Per-entry status lands in hdr.result (query.c:112). Check both.
 */
[[nodiscard]] int rt_ring_probe(struct io_uring_query_opcode *out);

/* Create the ring and map it. Returns 0, or -errno.
 *
 * Zero the io_uring_params first — resv[3] is checked and setup fails if it
 * is not clear. Then io_uring_setup(entries, &params) returns the ring fd.
 *
 * Then the mappings, where the sizing is the whole difficulty:
 *
 * SINGLE_MMAP is unconditional on this kernel — features is assigned
 * IORING_FEAT_FLAGS with no branch (io_uring.c:3058), and that constant
 * includes IORING_FEAT_SINGLE_MMAP (io_uring.h:35). So the SQ and CQ rings are
 * one mapping at IORING_OFF_SQ_RING, and the two-mapping path is dead code
 * here. Assert the bit rather than writing a fallback you can never test.
 *
 * Size it from the furthest thing inside it, which is the CQE array: the
 * offset the kernel reported for the CQEs, plus one CQE per cq entry. Do not
 * reach for the sq_off.array-based formula that liburing uses for this — under
 * NO_SQARRAY the kernel never assigns sq_off.array at all (io_uring.c:2941),
 * so it is zero, and the arithmetic silently produces a mapping far too small.
 *
 * SQEs are a separate mapping regardless, at IORING_OFF_SQES, sized by the
 * SQE type rather than by 64. MAP_POPULATE there: the SQEs are touched on
 * every submission, and faulting them in lazily buys nothing.
 *
 * MAP_SHARED on everything — this memory is shared with the kernel, and
 * MAP_PRIVATE would give you a private copy that the kernel never sees.
 *
 * Every pointer in rt_ring is then base + the byte offset the kernel reported.
 * Use the offsets, never a hand-computed layout of struct io_rings; it is an
 * internal type and is not in the uapi headers for exactly this reason.
 */
[[nodiscard]] int rt_ring_setup(struct rt_ring *r, unsigned entries);

/* Hand out the next free SQE, or nullptr if the SQ is full.
 *
 * Under NO_SQARRAY the slot is the tail masked by the ring mask, with no
 * indirection (io_uring.c:1990-1996).
 *
 * Two things to get right. The SQ is full when tail minus head equals the
 * entry count — unsigned wraparound makes that subtraction correct even when
 * tail has wrapped and head has not, which is why the counters are free-running
 * rather than pre-masked. And the slot you return is a *reused* one still
 * holding the last request that occupied it, so it must be cleared before the
 * caller fills in three fields and leaves the rest stale.
 */
[[nodiscard]] struct io_uring_sqe *rt_ring_sqe(struct rt_ring *r);

/* Publish the SQ tail and enter the kernel. Returns how many SQEs
 * the kernel consumed, or -errno.
 *
 * The tail store is the release: everything written into the SQE must be
 * visible to the kernel before the tail that exposes it. On aarch64 a plain
 * store here is not a style question — the CPU may reorder it ahead of the SQE
 * writes and the kernel will read a half-built request.
 *
 * Then enter with GETEVENTS, which under DEFER_TASKRUN is also what runs the
 * completion work. Passing min_complete makes the same call wait; submit and
 * wait are one enter.
 */
[[nodiscard]] int rt_ring_submit_and_wait(struct rt_ring *r,
                                          unsigned to_submit,
                                          unsigned min_complete);

/* Take one CQE. Returns false if the CQ is empty.
 *
 * The mirror of the submit publish: an acquire load of the CQ tail, because the CQE the
 * kernel wrote must not be read before the tail that announced it; then a
 * release store of the head, because the slot must not be seen as free until
 * you have finished copying out of it.
 *
 * Copy the CQE out rather than returning a pointer into the ring. The moment
 * the head advances, that slot belongs to the kernel again, and a caller
 * holding a pointer into it has a use-after-free with no allocator involved.
 */
[[nodiscard]] bool rt_ring_reap(struct rt_ring *r, struct io_uring_cqe *out);

#endif /* RT_RING_H */
