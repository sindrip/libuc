/*
 * UBSan minimal-runtime handlers. The runtime compiles with
 * -fsanitize=undefined -fsanitize-minimal-runtime -fno-sanitize-recover=all,
 * which emits calls to __ubsan_handle_<check>_minimal_abort — one undefined
 * symbol per check kind the code actually contains. Nothing provides them;
 * this file does.
 *
 * Add handlers lazily: a link error names precisely the one you need, so
 * there is no guessing and no dead code. The minimal runtime passes no
 * arguments — a handler can only say which check fired and where it was
 * called from, and __builtin_return_address(0) is the where. That is enough:
 * System.map and -no-pie make it resolvable under the debugger. Every
 * handler is the same two lines through rt_panic.
 *
 * With no test harness, this is the only mechanism that catches UB that does
 * not happen to segfault.
 *
 * TODO(7): this file starts with no handlers on purpose. Turning the flags
 * on in the container build (build/kernel.Dockerfile, the runtime-build
 * stage — host `make check` already carries them via the probe) produces the
 * link errors that name the first ones. RT-007 owns that flag change.
 */

#include "crash.h"
