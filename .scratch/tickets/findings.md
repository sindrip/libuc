# Cross-ticket implementation findings

Status: **current durable findings, 2026-08-30.** Ticket-specific outcomes stay
in their tickets. This file holds measured ABI/toolchain facts and deferred
choices used by more than one ticket.

## Linker-script permission grouping

The probe linker script groups R, RX, and RW output on `MAXPAGESIZE`
boundaries. That only works for sections the script names. Before `.got` was
captured, LLD placed the orphan directly after `.text`; its small RW load shared
the text page, the later mapping removed execute permission, and the process
faulted on the first instruction of `_start`.

`libuc.ld` now captures `.got` and `.got.plt` in the RW group. A remaining
hardening candidate is `--orphan-handling=error` for probes. It needs its own
linker-inventory check before landing: deliberately retained non-allocated
sections must not turn the protection into a blanket build failure.

The first read-only output section also emits one byte deliberately. Without it,
an executable with otherwise-empty rodata allowed LLD to begin the first load at
`.text`, leaving the program-header table supplied through `AT_PHDR` unmapped.

## Toolchain traps

- `thread_local` is a C23 keyword and cannot name a member.
- `-Wpadded` is active through `-Weverything`; structs order fields densely or
  spell required padding explicitly.
- clang-format can rewrite whitespace inside macro-stringized assembly operands
  and thereby change the emitted template. Inline-assembly blocks that depend on
  stringization retain `clang-format off/on` fences.
- A covered enum switch intentionally has no `default`: the warning policy keeps
  `-Wcovered-switch-default` and suppresses the contradictory
  `-Wswitch-default`, making a new enumerator fail every dispatch site.

## Measured TLS ABI facts

These were checked by disassembly and execution with the project toolchain.

- AArch64 uses TLS variant 1. The thread pointer addresses a 16-byte TCB and the
  TLS image begins above `round_up(16, p_align)`.
- x86-64 uses variant 2. The TLS image ends below the thread pointer; even a
  one-byte TLS object may use offset `-1`.
- Taking a thread-local address on x86-64 reads the self pointer at `%fs:0`, so
  block creation initializes that word before installation.
- The current common TCB is two pointer-sized words: self then current fiber.
  AArch64's ABI-fixed 16-byte TCB fits it; x86-64 owns the positive-offset TCB
  space without moving compiled negative TLS offsets.
- The architecture placement helper is the only code allowed to derive TLS
  block, TCB, and thread-pointer geometry.

## UC-011 storage decision

Thread-local block creation currently owns a mapping and fibers own that handle
by value. This was a staging choice, not a permanent allocation boundary.

UC-011 compares:

- separate stack and TLS mappings, which preserve fault isolation and simple
  lifetimes;
- one scheduler-owned carved allocation, which saves mapping/VMA churn and
  makes warm recycle a TLS image reset.

The change remains private if fiber code reaches TLS only through the handle and
installed thread pointer. UC-011 must first make scheduler ownership explicit;
the current caller-owned fiber API cannot reach a scheduler pool by itself.

## Ready-queue ownership

The current scheduler uses one intrusive link in each fiber. Requeue after
`YIELD` is therefore infallible and requires no capacity policy. Accepted costs:

- one link supports only one queue membership at a time;
- double enqueue is an unchecked contract violation;
- another simultaneously applicable scheduler-owned list would require another
  link or a different queue representation.

Only scheduler code mutates the ready queue. A future wake operation should name
the wait object and let the scheduler perform the ownership transfer rather than
hand a raw fiber pointer through an unowned intermediate state.

## Dispatch and operation seams

The scheduler currently reuses `__libuc_fiber_resume` per turn. A specialized
scheduler switch could avoid reconstructing the caller context and restoring
the bootstrap thread pointer on every dispatch, but it duplicates the switch
protocol. It lands only if measurement identifies dispatch as material.

The current CQE key is a fiber pointer and the reap path accepts exactly one CQE
per await. UC-016 replaces it with a generation-bearing operation record. The
operation slab is its own identity space; it may refer to a fiber, never be
embedded in a fiber slot merely to save an address calculation.

## Visibility policy

Static libuc does not yet need a hidden/default symbol policy. If a shared
library ever lands, the preferred failure mode is `-fvisibility=hidden`
globally with the public surface marked default: forgetting an export fails a
consumer link, whereas forgetting to hide an internal symbol silently freezes
an accidental ABI.

## Running probes outside the VM

Static AArch64 probes can execute in an AArch64 Linux container; x86-64 probes
need FSGSBASE-capable hardware for behavioral TLS acceptance. Any container
running scheduler startup must allow io_uring: Docker's default seccomp profile
denies its syscalls, so the acceptance container is deliberately unconfined.

The VM remains authoritative for PID-1 behavior and reports the probe status in
the attempted-to-kill-init panic's `exitcode=` field until a resident runtime
replaces returning probes.
