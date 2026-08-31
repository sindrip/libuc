#include "scheduler.h"

#include <stdint.h>

#include <linux/io_uring.h>
#include <linux/mman.h>

#include "syscall.h"

/* One record per parked fiber, and park stops at the CQ's capacity —
 * the default grant is twice the SQ — so this keeps the CQ the binding
 * bound rather than the table. */
constexpr uint32_t scheduler_operation_slots =
    2 * __libuc_scheduler_ring_entries;
static_assert(scheduler_operation_slots == 2048);
static_assert(scheduler_operation_slots <= 1U << __libuc_operation_slot_bits);

constexpr size_t scheduler_table_length =
    scheduler_operation_slots * sizeof(struct __libuc_operation);

[[nodiscard]] bool
__libuc_scheduler_become(struct __libuc_scheduler *scheduler) {
  const long address =
      __libuc_sys_mmap(nullptr, scheduler_table_length, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (__libuc_syscall_failed(address)) {
    return false;
  }

  *scheduler = (struct __libuc_scheduler){
      .table = (struct __libuc_operation *)(uintptr_t)address,
  };
  for (size_t slot = scheduler_operation_slots; slot != 0; slot--) {
    scheduler->table[slot - 1].next = scheduler->free_head;
    scheduler->free_head = &scheduler->table[slot - 1];
  }

  if (!__libuc_ring_create(&scheduler->ring, __libuc_scheduler_ring_entries)) {
    (void)__libuc_sys_munmap(scheduler->table, scheduler_table_length);
    return false;
  }

  return true;
}

void __libuc_scheduler_enqueue(struct __libuc_scheduler *scheduler,
                               struct __libuc_fiber *fiber) {
  scheduler->ready_count++;
  fiber->ready_next = nullptr;

  if (scheduler->ready_tail == nullptr) {
    scheduler->ready_head = fiber;
  } else {
    scheduler->ready_tail->ready_next = fiber;
  }
  scheduler->ready_tail = fiber;
}

static struct __libuc_operation *
allocate_operation(struct __libuc_scheduler *scheduler,
                   struct __libuc_fiber *waiter) {
  struct __libuc_operation *record = scheduler->free_head;
  if (record == nullptr) {
    __builtin_trap();
  }
  scheduler->free_head = record->next;

  record->waiter = waiter;
  record->state = __LIBUC_OPERATION_STATE_ACTIVE;

  return record;
}

static void release_operation(struct __libuc_scheduler *scheduler,
                              struct __libuc_operation *record) {
  record->generation++;
  record->state = __LIBUC_OPERATION_STATE_FREE;
  record->next = scheduler->free_head;
  scheduler->free_head = record;
}

static void park(struct __libuc_scheduler *scheduler,
                 struct __libuc_fiber *fiber) {
  /* IOSQE flags link batches or skip CQEs; none is designed. And a full
   * CQ lets the kernel drop wakes behind a masked -EBADR, so parking
   * stops at the CQ's capacity. */
  if (fiber->await_sqe->flags != 0 ||
      scheduler->parked_count == scheduler->ring.cq_entries) {
    __builtin_trap();
  }

  struct io_uring_sqe *slot = __libuc_ring_append_sqe(&scheduler->ring);
  if (slot == nullptr) {
    if (__libuc_syscall_failed(__libuc_ring_submit(&scheduler->ring, 0))) {
      __builtin_trap();
    }
    slot = __libuc_ring_append_sqe(&scheduler->ring);
    if (slot == nullptr) {
      __builtin_trap();
    }
  }

  struct __libuc_operation *record = allocate_operation(scheduler, fiber);

  *slot = *fiber->await_sqe;
  slot->user_data = __libuc_operation_key_pack((struct __libuc_operation_key){
      .generation = record->generation,
      .slot = (uint64_t)(record - scheduler->table),
      .tag = __LIBUC_OPERATION_TAG_RESULT,
  });
  scheduler->parked_count++;
}

void __libuc_scheduler_run(struct __libuc_scheduler *scheduler) {
  while (scheduler->parked_count + scheduler->ready_count != 0) {
    for (uint32_t turns = scheduler->ready_count; turns != 0; turns--) {
      struct __libuc_fiber *fiber = scheduler->ready_head;
      scheduler->ready_head = fiber->ready_next;
      scheduler->ready_count--;

      if (scheduler->ready_head == nullptr) {
        scheduler->ready_tail = nullptr;
      }
      fiber->ready_next = nullptr;

      switch (__libuc_fiber_resume(fiber)) {
      case __LIBUC_FIBER_REQUEST_YIELD:
        __libuc_scheduler_enqueue(scheduler, fiber);
        break;
      case __LIBUC_FIBER_REQUEST_EXIT:
        break;
      case __LIBUC_FIBER_REQUEST_AWAIT:
        park(scheduler, fiber);
        break;
      case __LIBUC_FIBER_REQUEST_NONE:
        __builtin_trap();
      }
    }

    if (scheduler->parked_count == 0) {
      continue;
    }

    const uint32_t min_complete = scheduler->ready_count == 0 ? 1 : 0;
    if (__libuc_syscall_failed(
            __libuc_ring_submit(&scheduler->ring, min_complete))) {
      __builtin_trap();
    }

    struct io_uring_cqe completion;
    while (__libuc_ring_reap(&scheduler->ring, &completion)) {
      /* Single-shot contract: every CQE is the sole completion of a parked
       * fiber's SQE. Streams and gap fillers trap until designed. */
      if (completion.flags &
          (IORING_CQE_F_MORE | IORING_CQE_F_NOTIF | IORING_CQE_F_SKIP)) {
        __builtin_trap();
      }

      const struct __libuc_operation_key key =
          __libuc_operation_key_unpack(completion.user_data);
      if (key.slot >= scheduler_operation_slots) {
        __builtin_trap();
      }

      /* Repacking the record's own identity compares tag, slot, and the
       * generation bits the key carries in one test. */
      struct __libuc_operation *record = &scheduler->table[key.slot];
      if (record->state != __LIBUC_OPERATION_STATE_ACTIVE ||
          completion.user_data !=
              __libuc_operation_key_pack((struct __libuc_operation_key){
                  .generation = record->generation,
                  .slot = key.slot,
                  .tag = __LIBUC_OPERATION_TAG_RESULT,
              })) {
        __builtin_trap();
      }

      struct __libuc_fiber *woken = record->waiter;
      woken->await_res = completion.res;
      release_operation(scheduler, record);

      scheduler->parked_count--;
      __libuc_scheduler_enqueue(scheduler, woken);
    }
  }
}
