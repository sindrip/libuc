/*
 * The minimal-runtime UBSan handlers. Under -fsanitize-minimal-runtime with
 * -fno-sanitize-recover=all, every failed check lowers to an argumentless
 * call to one of these __ubsan_handle_* symbols — the _minimal_abort
 * variants, except the two at the bottom whose ABI has only a bare
 * _minimal form. compiler-rt would normally provide them; freestanding,
 * they are ours to define.
 * Until libuc has a crash reporter, a failed check lands on a trap
 * instruction: loud, precise under a debugger, impossible to mistake for
 * success.
 */

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

/* Two checks have no _minimal_abort form in the ABI: an always-fatal check
 * gets no separate abort spelling, so clang calls the bare _minimal name
 * and that spelling must exist (verified against the pinned clang 22: C23
 * unreachable() emits __ubsan_handle_builtin_unreachable_minimal). */
#define LIBUC_UBSAN_HANDLER_NORECOVER(name)                                    \
  [[noreturn]] void __ubsan_handle_##name##_minimal(void);                     \
  [[noreturn]] void __ubsan_handle_##name##_minimal(void) { __builtin_trap(); }

LIBUC_UBSAN_HANDLER_NORECOVER(builtin_unreachable)
LIBUC_UBSAN_HANDLER_NORECOVER(missing_return)
