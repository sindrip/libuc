/*
 * UBSan minimal-runtime handlers. The runtime compiles with
 * -fsanitize=undefined -fsanitize-minimal-runtime -fno-sanitize-recover=all,
 * and links with -fno-sanitize-link-runtime so clang's own runtime archive
 * stays out (invariant 4's spirit: nothing linked that we did not write).
 * The instrumentation calls __ubsan_handle_<check>_minimal_abort — one
 * undefined symbol per check kind the code actually contains — and this
 * file is the runtime that answers.
 *
 * Handlers are added lazily: a link error names precisely the one you need.
 * The minimal runtime passes no arguments — a handler can only say which
 * check fired and where it was called from, and __builtin_return_address(0)
 * is the where, resolvable against the binary since -no-pie. That is
 * enough: with no test harness, this is the only mechanism that catches UB
 * that does not happen to segfault.
 *
 * The double-underscore names are the sanitizer's ABI, not a namespace
 * violation: whoever defines the runtime hooks IS the implementation, and
 * this file is exactly that.
 */

#include "crash.h"

#define UBSAN_ABORT(sym, msg)                                                  \
  [[noreturn]] void sym(void);                                                 \
  [[noreturn]] void sym(void) {                                                \
    rt_panic("ubsan: " msg, __builtin_return_address(0));                      \
  }

/* -Wreserved-identifier is right in general and wrong in exactly this file:
 * defining these reserved names is its entire purpose (see the header
 * comment). The suppression is scoped to the definitions and nothing else. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-identifier"

UBSAN_ABORT(__ubsan_handle_add_overflow_minimal_abort, "add overflow")
UBSAN_ABORT(__ubsan_handle_alignment_assumption_minimal_abort,
            "alignment assumption")
UBSAN_ABORT(__ubsan_handle_builtin_unreachable_minimal, "unreachable reached")
UBSAN_ABORT(__ubsan_handle_negate_overflow_minimal_abort, "negate overflow")
UBSAN_ABORT(__ubsan_handle_pointer_overflow_minimal_abort, "pointer overflow")
UBSAN_ABORT(__ubsan_handle_shift_out_of_bounds_minimal_abort,
            "shift out of bounds")
UBSAN_ABORT(__ubsan_handle_type_mismatch_minimal_abort, "type mismatch")

#pragma clang diagnostic pop
