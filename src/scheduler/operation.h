#ifndef LIBUC_SRC_SCHEDULER_OPERATION_H
#define LIBUC_SRC_SCHEDULER_OPERATION_H

#include <stdint.h>

struct __libuc_fiber;

/* Tag zero is invalid on purpose: a CQE_MIXED gap filler carries
 * user_data == 0, which must never decode into a live record. */
enum __libuc_operation_tag : uint64_t {
  __LIBUC_OPERATION_TAG_RESULT = 1,
};

struct __libuc_operation_key {
  uint64_t generation;
  uint64_t slot;
  enum __libuc_operation_tag tag;
};

enum __libuc_operation_state : uint8_t {
  __LIBUC_OPERATION_STATE_FREE,
  __LIBUC_OPERATION_STATE_ACTIVE,
};

struct __libuc_operation {
  struct __libuc_fiber *waiter;
  /* Free-list link while FREE. */
  struct __libuc_operation *next;
  uint64_t generation;
  enum __libuc_operation_state state;
  uint64_t : 56;
};

constexpr uint64_t __libuc_operation_tag_bits = 4;
constexpr uint64_t __libuc_operation_slot_bits = 16;
constexpr uint64_t __libuc_operation_tag_mask =
    (1ULL << __libuc_operation_tag_bits) - 1;
constexpr uint64_t __libuc_operation_slot_mask =
    (1ULL << __libuc_operation_slot_bits) - 1;

static inline uint64_t
__libuc_operation_key_pack(struct __libuc_operation_key key) {
  return key.tag | (key.slot << __libuc_operation_tag_bits) |
         (key.generation << (__libuc_operation_tag_bits +
                             __libuc_operation_slot_bits));
}

static inline struct __libuc_operation_key
__libuc_operation_key_unpack(uint64_t packed) {
  return (struct __libuc_operation_key){
      .generation =
          packed >> (__libuc_operation_tag_bits + __libuc_operation_slot_bits),
      .slot =
          (packed >> __libuc_operation_tag_bits) & __libuc_operation_slot_mask,
      .tag = (enum __libuc_operation_tag)(packed & __libuc_operation_tag_mask),
  };
}

#endif
