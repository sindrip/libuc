---
id: RT-004
title: Stackful context switch + task struct
status: done
depends: [RT-002, RT-003]
---

## Goal

Two stacks, and the ability to move between them deterministically. This is the
single most bug-prone file in the project and it is deliberately isolated from
the ring so that a failure here is unambiguous.

## Spec

### What must be saved (AAPCS64)

Only the **callee-saved** set. The caller has already spilled anything else it
cared about, because from C's perspective `rt_switch` is an ordinary function
call.

| registers | count | notes |
|---|---|---|
| `x19`–`x28` | 10 | callee-saved general purpose |
| `x29` (FP) | 1 | frame pointer |
| `x30` (LR) | 1 | return address — this is what resumption jumps to |
| `sp` | 1 | via a scratch register; `str sp` is not encodable |
| `d8`–`d15` | 8 | **only the low 64 bits of v8–v15 are callee-saved** |

Total 21 × 8 = **168 bytes**. Do *not* round to 176 for "16-byte alignment" —
an earlier draft of this ticket said to, and it was wrong. Two different rules
share that number:

- **`sp` must be 16-byte aligned** at every instruction that uses it. This is
  architectural, and it is why `start.S` does `and sp, x0, #-16`.
- **`stp`/`ldp` require no such thing.** On Normal memory they permit unaligned
  access outright; the struct's natural 8-byte alignment already exceeds what
  the instruction asks for.

`rt_ctx` is never used *as* a stack — it is a plain struct that `x0`/`x1` point
at — so the `sp` rule does not apply. Ground truth: the kernel's own
`cpu_context` (`out/src/arch/arm64/include/asm/processor.h:136-150`) is 13
`unsigned long` = **104 bytes**, not a multiple of 16, and it is exactly what
`cpu_switch_to` (`entry.S:821`) drives with `stp`/`ldp`.

Assert 168: it catches accidental padding. Asserting 176 would instead *require*
8 bytes of padding whose only job is satisfying an assertion that was wrong.

Saving `q8`–`q15` (full 128-bit) instead would be wrong-but-harmless; saving
only `x19`–`x30` and skipping `d8`–`d15` is wrong-and-silent — it corrupts
floating point across switches in a way that surfaces much later. Save them.

### `src/switch.S`

```
// void rt_switch(struct rt_ctx *from, struct rt_ctx *to);
//   x0 = from, x1 = to
rt_switch:
    stp  x19, x20, [x0, #0]
    stp  x21, x22, [x0, #16]
    stp  x23, x24, [x0, #32]
    stp  x25, x26, [x0, #48]
    stp  x27, x28, [x0, #64]
    stp  x29, x30, [x0, #80]
    mov  x2, sp
    str  x2,       [x0, #96]
    stp  d8,  d9,  [x0, #104]
    stp  d10, d11, [x0, #120]
    stp  d12, d13, [x0, #136]
    stp  d14, d15, [x0, #152]

    ldp  x19, x20, [x1, #0]
    ... (mirror)
    ldr  x2,       [x1, #96]
    mov  sp, x2
    ldp  d14, d15, [x1, #152]
    ret                       // returns into the *other* context's x30
```

`ret` is the switch. It jumps to the restored `x30`, which for a resumed task is
wherever *it* called `rt_switch` from.

### First entry into a new task

A freshly created task has never called `rt_switch`, so there is no saved `x30`
to return into. Prime it: set the new context's `x30` to a trampoline
(`rt_task_entry`) and stash the task's function pointer and argument in `x19`
and `x20`, which the trampoline reads. The trampoline calls the function and,
when it returns, marks the task dead and switches back to the scheduler — it
must **never** `ret` off the end of a task stack.

Prime the new context's `x29` to **zero**, and have the trampoline establish
its frame with that null `x29` as the saved frame pointer, for the same reason
`start.S` zeroes it: RT-007's crash handler walks the FP chain and stops at
null. Every task stack is a new chain; a root frame inheriting a stale `x29`
sends the walker into another task's dead stack — a backtrace that lies is
worse than none. Acceptance test 5 should also confirm the guard-page fault's
backtrace terminates at the task root, not in the scheduler.

### Stacks

```c
// 64 KiB usable + one guard page below it
void *base = mmap(NULL, GUARD + STACK, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
mprotect((char *)base + GUARD, STACK, PROT_READ|PROT_WRITE);
```

The guard page stays `PROT_NONE`, so stack overflow is an immediate `SIGSEGV` at
a known address rather than silent corruption of whatever is below. Given
hand-rolled asm and no libc, this is worth the one extra page per task.

The initial `sp` is the **top** of the usable region, 16-byte aligned. aarch64
requires `sp` 16-byte aligned at every instruction that uses it.

### `src/task.h`

```c
struct rt_ctx { unsigned long x19_28[10], fp, lr, sp; double d8_15[8]; };
static_assert(sizeof(struct rt_ctx) == 168);

struct rt_task {
    struct rt_ctx ctx;
    void        *stack_base;   // mmap base, including guard
    unsigned long stack_len;
    enum { RT_READY, RT_RUNNING, RT_BLOCKED, RT_DEAD } state;
    void (*fn)(void *);
    void *arg;
};
```

## Files

- `src/switch.S`
- `src/task.c`, `src/task.h`
- `src/main.c` — driver for this ticket

## Acceptance

Driven entirely from `rt_main`, no ring involved:

1. Spawn one task that `raw_write`s `A`, yields, writes `B`, yields, writes `C`,
   returns.
2. Scheduler writes `1` between each resume.
3. Console shows RT-003's `rt: alive`, then exactly `1A2B3C`, then a clean exit
   into the idle loop.
4. Under `./debug.sh` + `lldb`, a breakpoint in the task function shows an `sp`
   inside the task's mapped stack range, not the initial one.
5. Deliberately recurse until the guard page is hit; confirm `SIGSEGV` at the
   guard address rather than a hang or silent corruption.

### How 4 and 5 were actually verified

Both were done with temporary probes in `main.c` printing single characters,
not with `lldb`. Recorded because the ticket's stated methods do not work on
this kernel, and because the probes are better evidence.

**Criterion 5 — `ug` then `Attempted to kill init! exitcode=0x0000000b`.**
Recursion cannot prove what the criterion asks. If `mprotect` had wrongly
covered the guard page, the stack would run 4096 bytes further and fault below
the mapping instead: same `SIGSEGV`, same exit code, opposite conclusion. And
the address that would distinguish them is never printed — `show_unhandled_signals`
is `0` (`arch/arm64/kernel/traps.c:150`) and `CONFIG_SYSCTL` is off, so the
`debug.exception-trace` knob (`kernel/signal.c:4990`) is never registered and
cannot be set from the cmdline either.

Instead, write one byte at each end of the mapping with a marker before each:
`stack_base + stack_len - 1` must succeed, `stack_base` must fault. `ug` and a
panic proves both halves; `ug!` with no panic would have meant no guard page.

**Criterion 4 — `1iA2B3C`.** Compare `sp` against `stack_base ..
stack_base + stack_len` inside the task and print `i`/`o`. Reaching this needs
the task pointer, so pass it as `arg` — which also closes a gap in criterion 3,
where `arg` is `nullptr` and nothing exercises `ctx.gp[1]` -> `x20` -> `x0`.

An `lldb` software breakpoint on the task function does *not* work here: at the
initial stop the kernel is at its entry with the MMU off, so VA == PA, and the
runtime's `0x21xxxx` addresses are below QEMU `virt`'s RAM base of `0x40000000`.
The `BRK` write lands in unbacked space and is silently lost. A hardware
breakpoint (`br set -H`) avoids this; so does a temporary `brk #0` in the guest.

## Notes

Test 5 is not optional — it is the only proof the guard page is actually mapped
`PROT_NONE`, and an incorrectly-sized `mprotect` silently disables it.

**RT-003's banner is retained deliberately, and is part of criterion 3.** RT-004
is not a fresh start on top of RT-003 — it is built on it: `task.c` and `main.c`
both include `syscall.h`, `sys6` is an *edit* to that file, `start.S` is
unchanged and still the entry path, and every character of `1A2B3C` travels
through `raw_write` → `sys3` → `svc #0`.

That makes RT-003 the substrate this ticket's output rides on, so its acceptance
check re-runs for free on every boot — which is exactly what AGENTS.md requires
when shared code is touched, and with no regression net is the only thing that
does it.

It also buys a diagnostic the ticket otherwise lacks. Without the banner,
"nothing on the console" is ambiguous between *the syscall path is broken* and
*the first switch faulted before reaching `A`* — two failures with completely
different debugging paths, and the ambiguity lands precisely when you have just
edited `syscall.h` to add `sys6`. The failure table in `src/main.c`'s header
comment enumerates the rest.

If floating-point state seems fine while you're testing, that is expected: the C
in this ticket barely touches `d8`–`d15`. It will bite later, with no obvious
connection to the switch. Save them now.
