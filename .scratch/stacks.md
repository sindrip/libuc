# Task stacks and per-core memory

Status: **conversation-derived proposal, 2026-08-18. Not argued through in
plan.md.** Addresses plan.md's open question "per-core arena allocator design
and task stack sizing/guard pages". RT-004's 64 KiB + guard-page scheme is
correct for now; this documents where it goes as task counts grow.

## Density is three separate resources, not one ceiling

1. **Virtual address space — never the limit.** 1M tasks × 128 KiB reserved is
   128 GiB of a ~128 TiB user space. Reserve generously.
2. **Physical memory — demand paging plus park-time reclamation.** An untouched
   page costs nothing; a *parked* task's live stack is typically a few hundred
   bytes at its suspension point. `MADV_FREE` above a low watermark makes a
   long-parked task cost ~1 page + its task struct: 1M tasks ≈ 4–5 GiB, the
   same order as Go (≈ 2.5–4 GiB), the gap being 4 KiB page granularity vs
   Go's 2 KiB starting stacks. Two corrections to the naive version: reclaim
   **aged and batched** (an idle scan over cold tasks), never per-park — it is
   a page-table walk with TLB consequences and far too hot for the park path;
   and per invariant 1 it goes through `IORING_OP_MADVISE` (verified present:
   `out/src/io_uring/opdef.c:694`, impl `advise.c`), not a direct syscall.
   **Requires 4 KiB pages** — verified: `CONFIG_ARM64_4K_PAGES=y` in
   `out/kernel.config`; 16 KiB pages would quadruple the floor.
3. **VMA count and `mmap_lock` — the real limiter of the RT-004 scheme.** One
   mmap + guard per task is 2 VMAs per task against `vm.max_map_count`
   (default 65530, raisable to millions), and every map/madvise takes the
   process-global `mmap_lock` — the one lock a shared-nothing design cannot
   shard. Per-stack VMA traffic from N pinned cores serializes there.

## The dial: stack kind is a per-task property

| | default: fat guarded stack | dense: slab stack |
|---|---|---|
| layout | own mapping + `PROT_NONE` guard (RT-004) | one giant `MAP_NORESERVE` mapping, no guards |
| overflow | hardware fault at guard | prologue check emitted by the future compiler — 3–4 instructions (the limit must be loaded from the task's TLS/struct), or 2 with a dedicated register, which is Go's `g` answer |
| C frames | anywhere — FFI is a plain `bl` | none on this stack — FFI hops to a fat per-core C stack (~6 instructions, ~2–5 ns) |
| density | hundreds of thousands (VMA-count-bound) | millions (one VMA total, no VMA *creation* in steady state; reclamation madvise still takes `mmap_lock` in read mode — batch it) |

Not a global mode. A million parked connection-tasks in dense mode and a few
thousand FFI-heavy workers on fat stacks coexist under one scheduler.

The selection can be **inferred**: whether a task's entry function can reach
`extern "C"` is statically knowable, so the future language's effect system
("calls-C" as an inferred effect, alongside suspend/alloc) picks the stack
kind at spawn. No C reachable → dense costs nothing; C reachable → fat stack,
FFI stays free. See language.md — this is the load-bearing consequence of the
effect lattice.

**The borrowed-C-stack hazard.** A dense task's FFI hop parks its C frames on
the shared per-core C stack. If that C code calls back into language code that
*suspends*, the core's C stack is held by a parked task and every other dense
task's FFI deadlocks — this is exactly why Loom pins virtual threads on native
frames. Rule: code running on the borrowed stack must not suspend. Enforceable
at the import signature, since function types carry effects (language.md):
callbacks passed to C from a dense task must be non-`suspend`. The escape
hatch is lazy promotion — a dense task that needs suspending callbacks gets
promoted to an owned fat stack on first FFI.

**Stack pooling — required at any density.** The fat-stack limiter in practice
is lifecycle *churn*, not parked count: an mmap/munmap pair per task life
hammers the process-global `mmap_lock` from N pinned cores. Pool stacks per
core and reuse; never unmap in steady state. VMA count then bounds live +
pooled stacks and the lock traffic collapses to pool refills.

Until the language exists, the C runtime needs only the default scheme plus
per-core stack pooling, and — when task counts grow — aged batched `madvise`
and a raised `max_map_count`.

The Go/Loom alternative — copyable stacks with pointer maps, pinned while C
frames are live — remains available much later, buys stack *shrinking* on top,
and is not a prerequisite for millions.

## One stack size, because identity is derived from the address

`rt_fiber_current()` finds the running fiber by masking the frame address down
to a `RT_STACK_SIZE`-aligned block and reading a head parked at the top of it.
That removed the process-global the scheduler used to write before every
switch, and it is shared-nothing by construction: two threads running two
fibers read two different stacks, with no per-thread state to get wrong.

It also promotes `RT_STACK_SIZE` from a sizing choice into a load-bearing
constant. Three things ride on it now:

- **power of two** — `static_assert`ed in `fiber.c`;
- **every stack aligned to it** — enforced by the carve, which maps `2 *
  RT_STACK_SIZE` `PROT_NONE`, takes the aligned block inside, and unmaps both
  tails so the VMA count per fiber stays at two;
- **every stack exactly that size** — not enforceable by an assert, because it
  is a claim about every future allocation rather than about one constant.

**That third one contradicts the dial above.** Fat and dense stacks coexisting
under one scheduler means two sizes live at once, and a single compile-time
mask cannot serve both. So the dial is not reachable while fiber identity comes
from masking.

The way out is already named above, for a different reason: the dense-mode
overflow check needs its limit "from the task's TLS/struct, or 2 with a
dedicated register, which is Go's `g` answer." That register answers both
questions. Pin the current fiber into it and identity stops depending on the
address, stack-limit checks get their base for free, and sizes are free to
vary per task.

So the sequencing is: masking now, because it costs nothing and needs no TCB;
the register when a second stack class arrives, which is the same milestone
that forces it anyway. The move is small and local — `rt_fiber_current()` is
one function with six callers, all in `fiber.c` — and the magic word in the
head is what makes a mistake during that transition loud rather than silent.

## Task stack roots and the crash handler

Every task stack starts a new FP chain: prime `x29` to zero in the new
context and have the trampoline keep the null saved-`x29`, exactly as
`start.S` does, or RT-007's walker leaves the task's stack and produces a
backtrace that lies. Recorded in RT-004.

## The arena allocator (the other half of the open question)

- **Per-task bump arenas, freed wholesale at task death** — *after the reap
  rule*. Any arena memory referenced by an in-flight SQE stays kernel-visible
  until its CQE is reaped, cancellation included; teardown is cancel → drain →
  drop, never just drop (transport.md, "Buffer lifetime"). Kernel-visible
  receive buffers should come from per-core pools rather than task arenas so
  the common teardown has nothing to wait for. With that caveat: no per-object
  free in the common case; a request-shaped task's memory story is "bump,
  bump, die".
- **Per-core page pools** feed the arenas — pages come from and return to the
  owning core only (invariant 3), refilled from the kernel in batches to keep
  `mmap_lock` traffic rare.
- Long-lived allocations that outlive a task are the exception, not the model,
  and get an explicit per-core allocator when something actually needs one.
