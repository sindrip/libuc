---
id: RT-007
title: Crash handler — register dump + frame-pointer walk
status: todo
depends: [RT-003]
---

## Goal

When the runtime faults, say something useful. As PID 1 with no libc, no core
dumps, and no shell, an unhandled `SIGSEGV` is a silent hang or a panic with no
information.

## Spec

### Why this is not optional

The three highest-risk components — hand-rolled `switch.S`, hand-written ring
index arithmetic, and raw `mmap`'d stacks — all fail as memory corruption.
Without this, a corrupted context switch presents as a frozen VM.

### Handlers

Install for `SIGSEGV`, `SIGBUS`, `SIGILL`, and `SIGFPE` via `rt_sigaction`
(syscall 134 — verify against `out/src/include/uapi/asm-generic/unistd.h`) with
`SA_SIGINFO` and `SA_ONSTACK`.

`SA_ONSTACK` plus an alternate signal stack (`sigaltstack`) is what lets the
handler run **after a stack-overflow fault on the guard page** — the case you
most want reported. Without it the handler faults trying to push its own frame
and you get nothing.

Note: with no libc there is no `sigaction` wrapper; the raw syscall takes a
`sigset_t` size argument (8 on aarch64) and the `sa_restorer` field is required
on some architectures. On aarch64 the kernel provides the restorer via the vDSO,
but with `SA_RESTORER` unset you must confirm behaviour — this is a genuine
freestanding sharp edge, so test the handler fires before relying on it.

### What to dump

From `ucontext_t`'s `mcontext_t` (aarch64: `regs[31]`, `sp`, `pc`, `pstate`):

1. Signal number and `si_addr` (the faulting address).
2. `pc` and `pstate`.
3. `x0`–`x30` and `sp`.
4. A frame-pointer walk: `x29` is the frame pointer; each frame is
   `[fp] = saved fp`, `[fp+8] = saved lr`. Walk until `fp` is null, unaligned,
   or outside any known stack range.
5. Which task was running (`rt_current`) and its stack range, so "did we blow
   the guard page" is answerable at a glance.

Bound the walk (say 64 frames) and sanity-check each `fp` before dereferencing —
the handler must not fault while reporting a fault.

### Output path

`raw_write` only. This is the second reason the purity exception exists: the
ring may be exactly what is broken, and a handler that tries to submit an SQE to
report a corrupted ring will hang instead of reporting.

Write a fixed-size formatted buffer — no `printf`. A minimal hex-and-string
formatter is ~40 lines and is all this needs.

### After dumping

Do **not** return from the handler; the faulting instruction will just re-fault.
Halt in a tight loop so the state is inspectable under `./debug.sh`, or
`exit_group` to trigger a clean PID 1 panic. Prefer the halt loop — it preserves
the scene for the debugger.

### UBSan handlers — the second half of this ticket

**This ticket owns turning UBSan on.** RT-003 deliberately left it off: the flags
are in neither the host build (Apple clang has no minimal runtime) nor
`runtime-build`, because they emit undefined `__ubsan_handle_*` symbols that
nothing yet provides. Adding the flags and the handlers is one change, here.


The runtime builds with `-fsanitize=undefined -fsanitize-minimal-runtime
-fno-sanitize-recover=all` (Clang; GCC has no minimal-runtime mode). That emits
undefined `__ubsan_handle_<check>_minimal_abort` symbols — one per check kind the
code actually triggers — which **you** implement. Verified: a deliberate
shift-past-width in a freestanding `-nostdlib` build yields exactly

```
U __ubsan_handle_shift_out_of_bounds_minimal_abort
```

The minimal runtime passes no arguments, so a handler can only report *which*
check fired and where it was called from. That is enough:

```c
[[noreturn]] void __ubsan_handle_shift_out_of_bounds_minimal_abort(void) {
    rt_panic("ubsan: shift out of bounds", __builtin_return_address(0));
}
```

`rt_panic` shares the register dump and frame walk with the signal handlers.
`__builtin_return_address(0)` gives the faulting site, which `out/System.map`
and `-no-pie` make resolvable under the debugger.

Add handlers lazily — a link error names precisely the one you need, so there is
no guessing and no dead code. With no test harness, this is the only mechanism
that catches UB which does not happen to segfault.

## Files

- `src/crash.c`, `src/crash.h`
- `src/ubsan.c` — `__ubsan_handle_*_minimal_abort` handlers
- `src/fmt.c` — minimal hex/string formatter
- `src/main.c` — install early, before any task spawns

## Acceptance

- A deliberate null dereference in `rt_main` produces a dump naming `SIGSEGV`,
  `si_addr == 0`, a plausible `pc`, and at least two frames.
- A deliberate infinite recursion inside a task overflows onto the guard page
  and **still** produces a dump — this is the `SA_ONSTACK` proof, and it fails
  loudly if the alternate stack is misconfigured.
- The dump names the running task and its stack range.
- The frame walk terminates rather than looping (RT-003 zeroes `x29`/`x30` in
  `_start` for exactly this reason).
- Under `./debug.sh` the halt loop is reachable and registers match the dump.
- A deliberate `1 << 40` reports `ubsan: shift out of bounds` with a return
  address that resolves to the offending line under the debugger — proving the
  UBSan path works end to end, not just that it links.

## Notes

**Do this before RT-004, not after.** The numbering is not the order. Its only
dependency is RT-003, and testing is manual console inspection with no
regression net (see `AGENTS.md`), so a good failure report is the *only*
diagnostic the project has. Debugging hand-rolled `switch.S` without it means
reading a frozen VM.

The formatter written here is reusable for all later diagnostic output; don't
make it crash-specific.
