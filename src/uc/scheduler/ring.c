#include "ring.h"

#include <stdatomic.h>
#include <stdckdint.h>

#include <linux/errno.h>
#include <linux/io_uring.h>
#include <linux/mman.h>

#include "syscall.h"

static void *ring_at(void *base, uint32_t offset) {
  return (unsigned char *)base + offset;
}

[[nodiscard]] bool
__libuc_scheduler_ring_create(struct __libuc_scheduler_ring *ring,
                              uint32_t entries) {
  struct io_uring_params params = {0};
  params.flags = IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN |
                 IORING_SETUP_NO_SQARRAY | IORING_SETUP_SQ_REWIND |
                 IORING_SETUP_SUBMIT_ALL;

  const long descriptor = __libuc_sys_io_uring_setup(entries, &params);
  if (__libuc_syscall_failed(descriptor)) {
    return false;
  }
  if ((params.features & IORING_FEAT_SINGLE_MMAP) == 0) {
    return false;
  }

  size_t cqes_length;
  size_t ring_length;
  if (ckd_mul(&cqes_length, (size_t)params.cq_entries,
              sizeof(struct io_uring_cqe)) ||
      ckd_add(&ring_length, (size_t)params.cq_off.cqes, cqes_length)) {
    return false;
  }

  size_t sqes_length;
  if (ckd_mul(&sqes_length, (size_t)params.sq_entries,
              sizeof(struct io_uring_sqe))) {
    return false;
  }

  const long ring_mapping = __libuc_sys_mmap(
      nullptr, ring_length, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
      (int)descriptor, IORING_OFF_SQ_RING);
  if (__libuc_syscall_failed(ring_mapping)) {
    return false;
  }

  const long sqes_mapping = __libuc_sys_mmap(
      nullptr, sqes_length, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
      (int)descriptor, IORING_OFF_SQES);
  if (__libuc_syscall_failed(sqes_mapping)) {
    (void)__libuc_sys_munmap((void *)(uintptr_t)ring_mapping, ring_length);
    return false;
  }

  void *ring_base = (void *)(uintptr_t)ring_mapping;
  const uint32_t cq_mask =
      *(uint32_t *)ring_at(ring_base, params.cq_off.ring_mask);
  if (cq_mask != params.cq_entries - 1) {
    (void)__libuc_sys_munmap(ring_base, ring_length);
    (void)__libuc_sys_munmap((void *)(uintptr_t)sqes_mapping, sqes_length);
    return false;
  }

  *ring = (struct __libuc_scheduler_ring){
      .ring_mapping = ring_base,
      .ring_mapping_length = ring_length,
      .sqes = ring_at((void *)(uintptr_t)sqes_mapping, 0),
      .sqes_mapping_length = sqes_length,
      .cq_head = ring_at(ring_base, params.cq_off.head),
      .cq_tail = ring_at(ring_base, params.cq_off.tail),
      .cqes = ring_at(ring_base, params.cq_off.cqes),
      .descriptor = (int)descriptor,
      .sq_entries = params.sq_entries,
      .cq_entries = params.cq_entries,
      .cq_mask = cq_mask,
      .batch_count = 0,
  };
  return true;
}

[[nodiscard]] struct io_uring_sqe *
__libuc_scheduler_ring_append(struct __libuc_scheduler_ring *ring) {
  if (ring->batch_count == ring->sq_entries) {
    return nullptr;
  }

  struct io_uring_sqe *submission = &ring->sqes[ring->batch_count];
  *submission = (struct io_uring_sqe){0};
  ring->batch_count++;
  return submission;
}

static void shift_unsubmitted(struct __libuc_scheduler_ring *ring,
                              uint32_t submitted) {
  const uint32_t remaining = ring->batch_count - submitted;
  for (uint32_t index = 0; index < remaining; index++) {
    ring->sqes[index] = ring->sqes[submitted + index];
  }
  ring->batch_count = remaining;
}

[[nodiscard]] long
__libuc_scheduler_ring_submit(struct __libuc_scheduler_ring *ring,
                              uint32_t min_complete) {
  while (true) {
    const long result = __libuc_sys_io_uring_enter(
        ring->descriptor, ring->batch_count, min_complete,
        IORING_ENTER_GETEVENTS, nullptr, 0);
    if (result == -EINTR) {
      continue;
    }
    if (__libuc_syscall_failed(result)) {
      return result;
    }

    const uint32_t submitted = (uint32_t)result;
    if (submitted == ring->batch_count) {
      ring->batch_count = 0;
      return result;
    }

    shift_unsubmitted(ring, submitted);
  }
}

[[nodiscard]] bool
__libuc_scheduler_ring_reap(struct __libuc_scheduler_ring *ring,
                            struct io_uring_cqe *completion) {
  const uint32_t head =
      atomic_load_explicit(ring->cq_head, memory_order_relaxed);
  const uint32_t tail =
      atomic_load_explicit(ring->cq_tail, memory_order_acquire);
  if (head == tail) {
    return false;
  }

  *completion = ring->cqes[head & ring->cq_mask];
  atomic_store_explicit(ring->cq_head, head + 1, memory_order_release);
  return true;
}
