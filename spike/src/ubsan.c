#include "crash.h"

#define UBSAN_ABORT(sym, msg)                                                  \
  [[noreturn]] void sym(void);                                                 \
  [[noreturn]] void sym(void) {                                                \
    rt_panic("ubsan: " msg, __builtin_return_address(0));                      \
  }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-identifier"

UBSAN_ABORT(__ubsan_handle_add_overflow_minimal_abort, "add overflow")
UBSAN_ABORT(__ubsan_handle_alignment_assumption_minimal_abort,
            "alignment assumption")
UBSAN_ABORT(__ubsan_handle_builtin_unreachable_minimal, "unreachable reached")
UBSAN_ABORT(__ubsan_handle_negate_overflow_minimal_abort, "negate overflow")
UBSAN_ABORT(__ubsan_handle_out_of_bounds_minimal_abort, "out of bounds")
UBSAN_ABORT(__ubsan_handle_pointer_overflow_minimal_abort, "pointer overflow")
UBSAN_ABORT(__ubsan_handle_shift_out_of_bounds_minimal_abort,
            "shift out of bounds")
UBSAN_ABORT(__ubsan_handle_function_type_mismatch_minimal_abort,
            "function type mismatch")
UBSAN_ABORT(__ubsan_handle_load_invalid_value_minimal_abort,
            "load of invalid value")
UBSAN_ABORT(__ubsan_handle_type_mismatch_minimal_abort, "type mismatch")

#pragma clang diagnostic pop
