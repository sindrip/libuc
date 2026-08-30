# Fiber stacks and scheduler-owned storage

Status: **current input to UC-011, 2026-08-30.** Measurements from the retired
RT spike are retained where they answer sizing questions; its allocation and
identity mechanisms are not.

## Current implementation

Each `__libuc_fiber_create` performs two independent anonymous mappings:

1. a caller-sized, read-write stack with no guard page and no minimum size;
2. a thread-local block mapping sized and aligned from the executable's
   `PT_TLS` image plus the architecture's TCB placement.

Fiber destruction unmaps both. The root fiber currently requests 8 MiB; probes
request 64 or 256 KiB according to what they exercise. There is no stack pool,
arena, scheduler-owned fiber allocation, `MADV_FREE`, or resident teardown.

Fiber identity does not depend on stack address or size. The installed thread
pointer addresses the fiber's TCB, whose runtime word points at the current
fiber. Variable stack sizes therefore already work and must remain valid.

## What UC-011 must decide

The pool owner is a scheduler, never a CPU. Today's fiber API has no scheduler
argument and the fiber record is caller-owned, so ownership must become
structural before storage can be pooled.

Two thread-local layouts remain worth measuring:

| shape | mappings on a cold create | warm recycle | trade-off |
|---|---:|---|---|
| separate | stack plus TLS | stack can be pooled; TLS still has its own lifetime unless separately pooled | simple, fault-isolated, keeps layout concerns separate |
| carved | one scheduler-owned allocation containing stack and TLS placement | zero-fill plus TLS image copy | one mapping/VMA per fiber, but stack and TLS geometry become one allocator contract |

The comparison must include:

- `mmap`/`munmap` counts for cold create and warm recycle;
- VMAs per live and pooled fiber;
- bytes retained at the pool high-water mark;
- page faults and bytes touched during TLS reinitialization;
- support for a `PT_TLS` alignment larger than the page size;
- guard-page cost and whether a guard is enabled for all C-capable fibers;
- scheduler teardown: what is unmapped and how failures are reported.

Do not preserve `block_create` merely to preserve an internal API. Preserve the
architecture placement helpers: they are the sole authority for the relation
between TLS image, TCB, and thread pointer.

## Stack size evidence

The retired spike measured stack high-water marks by painting a hosted AArch64
thread stack and scanning it after representative vendored-library calls:

| workload | observed stack use |
|---|---:|
| empty thread | 272 B |
| xz preset 6 / 9 | 2.3 KiB |
| zstd level 1 / 9 / 19 | 6.5 / 7.0 / 7.2 KiB |
| zstd compress + decompress | 7.1 KiB |
| sqlite: 5k inserts, index, group-by | 9.9 KiB |
| sqlite: nested expression depth 100 / 500 / 900 | 14.8 / 15.6 / 15.2 KiB |

The evidence supports 64 KiB as a reasonable first pooled size for those
specific workloads, not as a universal ABI. The experiment used Homebrew
libraries on macOS, not the freestanding libuc build; libc, inlining, streaming
APIs, callbacks, and unbounded recursion can change the result. A public spawn
surface must either retain an explicit size or establish a measured default and
a larger-stack escape hatch.

## Density limits

Virtual address space is generous on the supported 64-bit targets; physical
pages and VMA churn are the relevant costs.

- Demand paging means untouched reserved stack pages consume no physical page.
- One mapping per stack plus one per separate TLS block makes mapping churn and
  VMA count scale with fiber lifecycle.
- Mapping and advice paths contend on process-wide MM state even when every
  scheduler otherwise owns disjoint memory.
- The pinned AArch64 kernel uses 4 KiB pages
  (`CONFIG_ARM64_4K_PAGES=y` in `out/kernel.config`). Reclaim granularity and
  guard cost must be recalculated on another page size.

Long-parked stack reclamation is a later optimization. If it lands, scan and
advise aged stacks in batches rather than calling it on every park, and use
`IORING_OP_MADVISE` (`out/src/io_uring/opdef.c:694`) rather than a direct
`madvise` syscall. Reclamation must retain the live portion below the saved
stack pointer and must not touch a stack referenced by an active kernel
operation.

Dense language-only stacks, compiler-emitted overflow checks, borrowed C
stacks, and stack copying belong to the deferred language/runtime design. They
are not inputs to UC-011 and should not complicate the first C-capable pool.

## Allocator boundary

A stack pool is not `malloc`. The future allocator must still answer whether
general allocations are fiber-scoped, scheduler-scoped, or explicitly
transferable. What carries over is the ownership rule: allocation and ordinary
free happen on the same scheduler, and kernel-visible memory is not reclaimed
until its operation reaches the terminal event described in `scheduler.md`.
