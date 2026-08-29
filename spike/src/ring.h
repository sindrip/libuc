#ifndef RT_RING_H
#define RT_RING_H

#include <stddef.h>

#include <linux/io_uring.h>
#include <linux/io_uring/query.h>

struct rt_ring {
  int fd;

  unsigned char *ring;
  size_t ring_len;
  struct io_uring_sqe *sqes;
  size_t sqes_len;

  _Atomic unsigned *sq_head;
  _Atomic unsigned *sq_tail;
  _Atomic unsigned *cq_head;
  _Atomic unsigned *cq_tail;

  struct io_uring_cqe *cqes;

  unsigned cached_sq_tail;

  unsigned sq_mask;
  unsigned cq_mask;

  unsigned features;
};

[[nodiscard]] int rt_ring_probe(struct io_uring_query_opcode *out);

[[nodiscard]] int rt_ring_setup(struct rt_ring *r, unsigned entries);

[[nodiscard]] struct io_uring_sqe *rt_ring_sqe(struct rt_ring *r);

[[nodiscard]] int rt_ring_submit_and_wait(struct rt_ring *r,
                                          unsigned to_submit,
                                          unsigned min_complete);

[[nodiscard]] bool rt_ring_reap(struct rt_ring *r, struct io_uring_cqe *out);

#endif
