#include "ring.h"

#include <stdatomic.h>
#include <stdint.h>

#include <asm/errno.h>
#include <linux/mman.h>

#include "syscall.h"

constexpr unsigned RT_RING_FLAGS = IORING_SETUP_SINGLE_ISSUER |
                                   IORING_SETUP_DEFER_TASKRUN |
                                   IORING_SETUP_NO_SQARRAY;

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

  auto r = sys_io_uring_register(-1, IORING_REGISTER_QUERY, &hdr, 0);
  if (sys_failed(r)) {
    return r;
  }

  return hdr.result;
}

int rt_ring_setup(struct rt_ring *r, unsigned entries) {

  struct io_uring_params params = {.flags = RT_RING_FLAGS};
  auto ret = sys_io_uring_setup(entries, &params);
  if (sys_failed(ret)) {
    return ret;
  }
  r->fd = ret;

  if (!(params.features & IORING_FEAT_SINGLE_MMAP)) {
    return -EOPNOTSUPP;
  }

  r->ring_len =
      params.cq_off.cqes + params.cq_entries * sizeof(struct io_uring_cqe);

  auto m = sys_mmap(nullptr, r->ring_len, PROT_READ | PROT_WRITE,
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

  r->cached_sq_tail = atomic_load_explicit(r->sq_tail, memory_order_relaxed);

  r->features = params.features;
  return 0;
}

struct io_uring_sqe *rt_ring_sqe(struct rt_ring *r) {

  auto pending = r->cached_sq_tail -
                 atomic_load_explicit(r->sq_head, memory_order_acquire);

  if (pending == r->sq_mask + 1) {
    return nullptr;
  }

  struct io_uring_sqe *sqe = &r->sqes[r->cached_sq_tail & r->sq_mask];

  *sqe = (struct io_uring_sqe){};

  r->cached_sq_tail++;
  return sqe;
}

int rt_ring_submit_and_wait(struct rt_ring *r, unsigned to_submit,
                            unsigned min_complete) {

  atomic_store_explicit(r->sq_tail, r->cached_sq_tail, memory_order_release);

  return sys_io_uring_enter(r->fd, to_submit, min_complete,
                            IORING_ENTER_GETEVENTS, nullptr, 0);
}

bool rt_ring_reap(struct rt_ring *r, struct io_uring_cqe *out) {

  auto head = atomic_load_explicit(r->cq_head, memory_order_relaxed);
  auto tail = atomic_load_explicit(r->cq_tail, memory_order_acquire);
  if (head == tail) {
    return false;
  }

  *out = r->cqes[head & r->cq_mask];

  atomic_store_explicit(r->cq_head, head + 1, memory_order_release);
  return true;
}
