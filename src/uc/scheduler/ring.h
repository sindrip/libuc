#ifndef LIBUC_SRC_UC_SCHEDULER_RING_H
#define LIBUC_SRC_UC_SCHEDULER_RING_H

#include <stddef.h>
#include <stdint.h>

#include <linux/io_uring.h>

struct __libuc_scheduler_ring {
  void *ring_mapping;
  size_t ring_mapping_length;
  struct io_uring_sqe *sqes;
  size_t sqes_mapping_length;

  _Atomic uint32_t *cq_head;
  _Atomic uint32_t *cq_tail;
  struct io_uring_cqe *cqes;

  int descriptor;
  uint32_t sq_entries;
  uint32_t cq_entries;
  uint32_t cq_mask;
  uint32_t batch_count;
};

[[nodiscard]] bool
__libuc_scheduler_ring_create(struct __libuc_scheduler_ring *ring,
                              uint32_t entries);

[[nodiscard]] struct io_uring_sqe *
__libuc_scheduler_ring_append(struct __libuc_scheduler_ring *ring);

[[nodiscard]] long
__libuc_scheduler_ring_submit(struct __libuc_scheduler_ring *ring,
                              uint32_t min_complete);

[[nodiscard]] bool
__libuc_scheduler_ring_reap(struct __libuc_scheduler_ring *ring,
                            struct io_uring_cqe *completion);

#endif
