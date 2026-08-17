---
id: RT-005
title: Raw io_uring setup + NOP round trip
status: todo
depends: [RT-003]
---

## Goal

Set up one ring by hand — no liburing — submit an `IORING_OP_NOP`, and reap its
CQE. No coroutines: a failure here is unambiguously ring code.

`NOP` is deliberate. It has no fd and touches no I/O path, so it isolates
submission/completion mechanics from everything else.

## Spec

### Setup flags

```
IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN | IORING_SETUP_NO_SQARRAY
```

- `SINGLE_ISSUER` (bit 12) — one thread owns this ring forever. True by
  construction in this design, and what `DEFER_TASKRUN` requires.
- `DEFER_TASKRUN` (bit 13) — completion work runs when *we* call
  `io_uring_enter` with `IORING_ENTER_GETEVENTS`, not at arbitrary syscall
  boundaries. Hard-requires `SINGLE_ISSUER`.
- `NO_SQARRAY` (bit 16) — **simpler**, not merely faster: there is no `array[]`
  indirection to maintain. SQ head/tail index the SQEs directly.

**Never `SQPOLL`.** Verified at `out/src/io_uring/io_uring.c:2815-2821`: it
rejects `COOP_TASKRUN`, `TASKRUN_FLAG`, and `DEFER_TASKRUN` with `-EINVAL`.

### Use the kernel's structs — do not retype them

`#include <linux/io_uring.h>` from the `headers_install` output (RT-003). The
structs are the pinned kernel's own, already SHA256-covered. Copying them by
hand would duplicate a definition that is in the repo and add a silent-layout
bug class for nothing.

Zero `struct io_uring_params` before the call — `resv[3]` and `resv1` must be
zero or setup fails.

### mmap the rings

```
IORING_OFF_SQ_RING  0x0
IORING_OFF_CQ_RING  0x8000000
IORING_OFF_SQES     0x10000000
```

Check `params.features & IORING_FEAT_SINGLE_MMAP` (bit 0). When set, the SQ and
CQ rings share **one** mapping — map `max(sq_len, cq_len)` once at
`IORING_OFF_SQ_RING` and derive the CQ pointers from the same base. Handling
only the two-mapping case will work and then leak a mapping forever; handling
only the single case will segfault on kernels without it. Handle both, or assert
the feature bit is present and refuse to start otherwise — the latter is
defensible here since the kernel is pinned.

SQEs are a separate mapping regardless: `sq_entries * sizeof(struct io_uring_sqe)`
at `IORING_OFF_SQES`, with `MAP_POPULATE`.

All mappings need `MAP_SHARED`.

### `static_assert` the assumptions the type system cannot express

Using the kernel's headers removes the transcription bug class, so asserting
struct sizes is now mostly noise — the compiler already agrees with itself.
Assert only where **your arithmetic hardcodes a relationship the types do not
capture**. There is exactly such a case here, and it is a trap:

```c
struct io_uring_cqe { __u64 user_data; __s32 res; __u32 flags; __u64 big_cqe[]; };
```

`big_cqe[]` is a flexible array member, so `sizeof(struct io_uring_cqe)` is **16
whether or not `IORING_SETUP_CQE32` is set** — under CQE32 the ring stride is 32.
Submission side is the same: `struct io_uring_sqe` ends in `__u8 cmd[0]`, so
`sizeof` stays 64 even under `IORING_SETUP_SQE128`, where the stride is 128.

**Ring stride comes from the setup flags, not from the type.** So
`static_assert(sizeof(struct io_uring_cqe) == 16)` proves nothing about the ring
you created — it passes happily while a reaping loop walks the CQ at half stride
and reads garbage from every other entry. Assert the real assumption instead:

```c
constexpr unsigned RING_FLAGS = IORING_SETUP_SINGLE_ISSUER
                              | IORING_SETUP_DEFER_TASKRUN
                              | IORING_SETUP_NO_SQARRAY;

static_assert((RING_FLAGS & (IORING_SETUP_SQE128 | IORING_SETUP_CQE32)) == 0,
              "stride arithmetic assumes 64-byte SQEs and 16-byte CQEs");
```

That fires the day someone adds `CQE32` without revisiting the reaping loop.

Everywhere else, use `sizeof`/`offsetof` directly rather than a magic number
defended by an assert — `sq_entries * sizeof(struct io_uring_sqe)`, never
`sq_entries * 64`.

### Submission and completion

Head and tail are shared with the kernel and need real ordering — this is what
`<stdatomic.h>` is for:

- Publishing a new tail: `atomic_store_explicit(tail, v, memory_order_release)`.
- Reading the kernel's CQ tail: `atomic_load_explicit(tail, memory_order_acquire)`.
- Publishing a consumed CQ head: release.

A plain store here is the classic io_uring bug: it works on x86 and fails on
aarch64, which is exactly what you're running.

Then `io_uring_enter(fd, to_submit, min_complete, IORING_ENTER_GETEVENTS, NULL, 0)`.

### Capability probe

Use `query.c`'s interface (`io_uring_query_opcode` → `nr_request_opcodes`,
`nr_register_opcodes`, `feature_flags`) at startup rather than hardcoding
version assumptions. Log what's available via `raw_write`; it costs one call and
makes later "why doesn't this opcode work" questions answerable.

### Failure reporting

`io_uring_setup` failure must be reported via `raw_write` — it cannot be
reported through the ring that just failed to exist. This is the sanctioned
purity exception from RT-003.

## Files

- `src/ring.c`, `src/ring.h` — mechanics only; structs come from
  `<linux/io_uring.h>`
- `src/main.c` — driver

## Acceptance

- `io_uring_setup` succeeds with all three flags; `features` is logged.
- All `static_assert`s pass at compile time.
- One `IORING_OP_NOP` submitted; `io_uring_enter` returns 1.
- Its CQE is reaped with `res == 0` and `user_data` equal to the sentinel set.
- Console shows the probe output and `nop ok`.
- Setup failure path exercised once (e.g. pass a bogus flag) and reports a
  decoded `-errno` rather than hanging.

## Notes

Do **not** add `SQ_REWIND` yet. It requires `NO_SQARRAY` (which you have) and
forbids touching head/tail at all, which only makes sense once submission is
batched once per scheduler turn. It's a simplification for later, not now.

`IORING_FEAT_NO_IOWAIT` (bit 17) exists on 7.2 and is new — worth logging from
the probe, not acting on yet.
