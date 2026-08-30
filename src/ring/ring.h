#ifndef LIBUC_SRC_RING_RING_H
#define LIBUC_SRC_RING_RING_H

#include <stddef.h>
#include <stdint.h>

#include <linux/io_uring.h>

struct __libuc_ring {
  void *ring_mapping;
  size_t ring_mapping_length;
  struct io_uring_sqe *sqes;
  size_t sqes_mapping_length;

  /* The kernel writes cq_tail; libuc writes cq_head. Loads of the kernel's
   * word acquire, stores of ours release. Under SQ_REWIND the SQ ring words
   * are never touched. */
  _Atomic uint32_t *cq_head;
  _Atomic uint32_t *cq_tail;
  struct io_uring_cqe *cqes;

  int descriptor;
  uint32_t sq_entries;
  uint32_t cq_entries;
  uint32_t cq_mask;
  /* SQEs written since the last enter; SQ_REWIND consumes them from index
   * zero, so this is both count and next slot. */
  uint32_t batch_count;
  uint32_t : 32;
};

[[nodiscard]] bool __libuc_ring_create(struct __libuc_ring *ring,
                                       uint32_t entries);

/* nullptr when the batch already fills the submission queue. */
[[nodiscard]] struct io_uring_sqe *
__libuc_ring_append_sqe(struct __libuc_ring *ring);

/* Submit the batch and wait for min_complete completions. Success consumes
 * the whole batch: a short kernel submission retries after the remainder
 * moves to slot zero, where SQ_REWIND rereads. Failure keeps the remainder
 * at slot zero, so retrying is submitting again. */
[[nodiscard]] long __libuc_ring_submit(struct __libuc_ring *ring,
                                       uint32_t min_complete);

/* false when the completion queue is empty. */
[[nodiscard]] bool __libuc_ring_reap(struct __libuc_ring *ring,
                                     struct io_uring_cqe *completion);

#endif
