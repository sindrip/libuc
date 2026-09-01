#ifndef LIBUC_UC_FIBER_H
#define LIBUC_UC_FIBER_H

struct uc_fiber;

typedef void *uc_fiber_entry(void *);

enum uc_fiber_result_kind {
  UC_FIBER_SUSPENDED,
  UC_FIBER_RETURNED,
};

struct uc_fiber_result {
  enum uc_fiber_result_kind kind;
  void *value;
};

[[nodiscard]] struct uc_fiber *uc_fiber_create(uc_fiber_entry *entry);

[[nodiscard]] struct uc_fiber_result uc_fiber_resume(struct uc_fiber *fiber,
                                                     void *value);

[[nodiscard]] void *uc_fiber_suspend(void *value);

void uc_fiber_destroy(struct uc_fiber *fiber);

#endif
