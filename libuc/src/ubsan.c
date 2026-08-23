#define LIBUC_UBSAN_HANDLER(name)                                              \
  [[noreturn]] void __ubsan_handle_##name##_minimal_abort(void);               \
  [[noreturn]] void __ubsan_handle_##name##_minimal_abort(void) {              \
    __builtin_trap();                                                          \
  }

LIBUC_UBSAN_HANDLER(add_overflow)
LIBUC_UBSAN_HANDLER(alignment_assumption)
LIBUC_UBSAN_HANDLER(divrem_overflow)
LIBUC_UBSAN_HANDLER(float_cast_overflow)
LIBUC_UBSAN_HANDLER(function_type_mismatch)
LIBUC_UBSAN_HANDLER(implicit_conversion)
LIBUC_UBSAN_HANDLER(invalid_builtin)
LIBUC_UBSAN_HANDLER(load_invalid_value)
LIBUC_UBSAN_HANDLER(mul_overflow)
LIBUC_UBSAN_HANDLER(negate_overflow)
LIBUC_UBSAN_HANDLER(nonnull_arg)
LIBUC_UBSAN_HANDLER(nonnull_return)
LIBUC_UBSAN_HANDLER(nullability_arg)
LIBUC_UBSAN_HANDLER(nullability_return)
LIBUC_UBSAN_HANDLER(out_of_bounds)
LIBUC_UBSAN_HANDLER(pointer_overflow)
LIBUC_UBSAN_HANDLER(shift_out_of_bounds)
LIBUC_UBSAN_HANDLER(sub_overflow)
LIBUC_UBSAN_HANDLER(type_mismatch)
LIBUC_UBSAN_HANDLER(vla_bound_not_positive)

#define LIBUC_UBSAN_HANDLER_NORECOVER(name)                                    \
  [[noreturn]] void __ubsan_handle_##name##_minimal(void);                     \
  [[noreturn]] void __ubsan_handle_##name##_minimal(void) { __builtin_trap(); }

LIBUC_UBSAN_HANDLER_NORECOVER(builtin_unreachable)
LIBUC_UBSAN_HANDLER_NORECOVER(missing_return)
