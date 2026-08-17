---
id: RT-004
title: Stackful context switch + task struct
status: todo
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

Total 21 × 8 = 168 bytes. Round the struct to 176 for 16-byte alignment.

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
static_assert(sizeof(struct rt_ctx) == 176);

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
3. Console shows exactly `1A2B3C` and then a clean exit into the idle loop.
4. Under `./debug.sh` + `lldb`, a breakpoint in the task function shows an `sp`
   inside the task's mapped stack range, not the initial one.
5. Deliberately recurse until the guard page is hit; confirm `SIGSEGV` at the
   guard address rather than a hang or silent corruption.

## Notes

Test 5 is not optional — it is the only proof the guard page is actually mapped
`PROT_NONE`, and an incorrectly-sized `mprotect` silently disables it.

If floating-point state seems fine while you're testing, that is expected: the C
in this ticket barely touches `d8`–`d15`. It will bite later, with no obvious
connection to the switch. Save them now.
