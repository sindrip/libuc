# aarch64 assembly — working reference

Scoped to what this runtime actually writes: `start.S`, `switch.S`, and inline
syscall asm. Everything below marked ✅/❌ was assembled with the project's own
clang, not recalled.

## Registers

| register | role (AAPCS64) | who preserves it |
|---|---|---|
| `x0`–`x7` | arguments and return values | caller |
| `x8` | indirect result location; **Linux syscall number** | caller |
| `x9`–`x15` | temporaries | caller |
| `x16`, `x17` | IP0/IP1, may be clobbered by linker veneers | caller |
| `x18` | platform register — do not use | — |
| `x19`–`x28` | general purpose | **callee** |
| `x29` | frame pointer (FP) | **callee** |
| `x30` | link register (LR) — where `ret` returns to | **callee** |
| `sp` | stack pointer | **callee** |
| `xzr` / `wzr` | reads as zero, writes discarded | — |
| `d8`–`d15` | low 64 bits of `v8`–`v15` | **callee** |
| `d0`–`d7`, `d16`–`d31` | SIMD/FP | caller |

**Callee-saved is the set a context switch must save**: `x19`–`x28`, `x29`,
`x30`, `sp`, and `d8`–`d15`. Everything else the caller has already spilled.

`wN` is the low 32 bits of `xN`. **Writing `wN` zeroes the upper 32 bits of
`xN`** — so `mov w8, #64` fully sets `x8`, which is why the compiler emits `w`
forms for small constants.

## `sp` is not a general register

Most instructions cannot take `sp` as an operand. Verified:

| | |
|---|---|
| `mov x0, sp` | ✅ |
| `mov sp, x0` | ✅ |
| `and sp, x0, #-16` | ✅ immediate form accepts `sp` as destination |
| `and sp, x0, x1` | ❌ shifted-register form does not |
| `sub sp, sp, #16` | ✅ |
| `stp x29, sp, [x8]` | ❌ — copy to a scratch register first |

The last one is why `cpu_switch_to` does `mov x9, sp` and then stores `x9`.

**`sp` must be 16-byte aligned** at every instruction that uses it. Linux hands
`_start` an aligned `sp` already; realigning is defensive, not required.

## Instructions this project needs

```
mov   xD, xN | #imm       move (aliases ORR/ADD; #imm has encoding limits)
movz/movk                 build a large constant in 16-bit chunks
add/sub  xD, xN, #imm     imm is 12-bit, optionally shifted left 12
and/orr/eor xD, xN, #imm  "logical immediate" — a bitmask pattern, not any value
ldr/str  xN, [xB, #imm]   load/store 64-bit
ldp/stp  xA, xB, [xB, #i] load/store PAIR — the context-switch workhorse
adrp/add                  form a PC-relative address (see below)
bl  label                 branch with link: sets x30, then jumps
ret                       jump to x30
b   label                 plain branch, no link
svc #0                    supervisor call — the syscall trap
wfe                       wait for event: idle the core cheaply
```

### Addressing modes for `ldp`/`stp`

```
stp x19, x20, [x8]          offset 0, x8 unchanged
stp x19, x20, [x8, #16]     offset, x8 unchanged
stp x19, x20, [x8, #16]!    PRE-index:  x8 += 16 first, then store
stp x19, x20, [x8], #16     POST-index: store, then x8 += 16
```

Post-index is what the kernel uses to walk a save area sequentially.

### `adrp` + `add`

A single instruction cannot hold a 64-bit address. `adrp` loads a 4KB-page-
aligned PC-relative address; `add` supplies the offset within the page:

```
adrp x1, .L.str
add  x1, x1, :lo12:.L.str
```

This is the pair clang emitted for the string literal in `raw_write`.

### Logical immediates

`and`/`orr`/`eor` immediates are not arbitrary — they must be an encodable
repeating bitmask pattern. `#-16` (0xFFFF…FFF0) is encodable. If the assembler
rejects a constant, build it with `movz`/`movk` into a register and use the
register form.

## Directives

```
.text                     code section
.globl name               make the symbol externally visible
.type  name, %function    mark it a function (for the debugger and linker)
.size  name, . - name     symbol size; `.` is the current address
.section .note.GNU-stack, "", %progbits    mark stack non-executable
```

`.S` (capital) is preprocessed, so `#include <asm/unistd.h>` works and syscall
numbers never get hardcoded. `.s` (lowercase) is not.

## References in this repo — prefer these to anything online

- **`out/src/arch/arm64/kernel/entry.S:821` — `cpu_switch_to`.** The kernel's
  own context switch, and the model for `switch.S`. Note the `stp … [x8], #16`
  post-index walk, and `mov x9, sp` before storing `sp`.

  **It does not save `d8`–`d15`**, because kernel code does not use FP/SIMD and
  the kernel saves that state separately. A userspace coroutine switch *must*
  save them. Copying `cpu_switch_to` verbatim silently corrupts floating point
  across switches, with the failure appearing far from the cause.

- `out/src/Documentation/arch/arm64/booting.rst` — machine state at kernel entry.
- `out/src/arch/arm64/lib/` — hand-written `memcpy`/`memset`, heavily optimised;
  useful to read, not to copy for RT-003.

## Authoritative external references

- [AAPCS64](https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst)
  — the procedure call standard: register roles, stack alignment, argument
  passing. The document that settles calling-convention arguments.
- [Arm A-profile A64 Instruction Set Architecture](https://developer.arm.com/documentation/ddi0602/latest/)
  — per-instruction encodings and operand constraints.
- [Arm Architecture Reference Manual (DDI 0487)](https://developer.arm.com/documentation/ddi0487/latest/)
  — the full ARM ARM. Exhaustive; use the ISA guide above first.

## Checking your own asm

```sh
make check                                    # assembles src/*.S
clang --target=aarch64-unknown-linux-gnu -c src/start.S -o /dev/null
```

To see what the compiler generates for a C construct — the fastest way to learn
an idiom is to write it in C and read the output:

```sh
clang --target=aarch64-unknown-linux-gnu -O1 -S -o - src/main.c
```
