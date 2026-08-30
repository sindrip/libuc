---
id: UC-008
title: Create the current task's ring
status: done
depends: []
---

## Goal

Reintroduce the smallest io_uring object needed by a scheduler, independently
of fiber queue policy.

## Spec

Add hand-written ring setup and mappings using only the pinned kernel UAPI.
Setup uses `IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN |
IORING_SETUP_NO_SQARRAY | IORING_SETUP_SQ_REWIND | IORING_SETUP_SUBMIT_ALL`
(SUBMIT_ALL so per-SQE preparation failure does not stop the batch,
`io_uring.c:2053-2061`; request-allocation short submissions remain possible
and are retried by the userspace ring); `IORING_SETUP_SQPOLL` is never accepted. The task that
calls setup is the ring's owner and remains its sole submitter.

The initial interface acquires an SQE, enters the ring with the batch, and
reaps a CQE. Submission uses `IORING_SETUP_SQ_REWIND` (2026-08-30 decision):
each batch is written from SQE index zero and consumed synchronously by
enter — `io_submit_sqes` takes `sq_entries` rather than the published tail
and `io_commit_sqring` rewinds instead of storing the consumed head
(`out/src/io_uring/io_uring.c:2029,1970`) — so no SQ head/tail protocol
exists and a batch is capped at `sq_entries`. CQ head/tail communication
uses the required acquire and release operations. Ring geometry and offsets
come from `struct io_uring_params`; no kernel structure or constant is
retyped.

`io_uring_setup`, `io_uring_enter`, and the ring mappings use the sanctioned
direct syscall path because no io_uring opcode can create or drive a ring. No
liburing, registered resources, resize, cancellation, multishot operation, or
ring teardown is introduced. The probe's process lifetime owns this first
ring; hosted teardown remains later work.

## Flag ledger (2026-08-30, every bit of IORING_SETUP_FLAGS)

| flag | verdict | why |
|---|---|---|
| `SINGLE_ISSUER` | chosen | invariant 3 as ABI; kernel binds ring to task (`tctx.c:202-204`) |
| `DEFER_TASKRUN` | chosen | invariant 7; completions only inside owner's `enter(GETEVENTS)` |
| `NO_SQARRAY` | chosen | legacy out-of-order indirection a sequential issuer cannot use |
| `SQ_REWIND` | chosen | batch-from-zero deletes SQ head/tail protocol (`io_uring.c:1970,2029`) |
| `SUBMIT_ALL` | chosen | continue past per-SQE preparation errors; allocation shorts are handled separately (`io_uring.c:2046-2068`) |
| `SQPOLL` | rejected | invariant 2; excludes DEFER_TASKRUN (`io_uring.c:2815-2821`); kernel thread is a second issuer |
| `SQ_AFF` | rejected | SQPOLL companion, moot |
| `COOP_TASKRUN` | rejected | task work still runs at any kernel entry; DEFER is the strong form |
| `TASKRUN_FLAG` | rejected | signals for coop/sqpoll modes; meaningless under DEFER |
| `CLAMP` | rejected | silently shrinks the ring; libuc fails loudly instead |
| `ATTACH_WQ` | rejected | shared io-wq couples schedulers; against shared-nothing |
| `R_DISABLED` | rejected | sandbox/restrictions machinery libuc does not use |
| `IOPOLL` | rejected | storage busy-poll burns the scheduler's CPU; a dedicated ring may revisit |
| `HYBRID_IOPOLL` | rejected | IOPOLL variant, same lane |
| `CQSIZE` | deferred | per-scheduler CQ sizing; default 2x SQ until a workload measurement exists |
| `NO_MMAP` | deferred | caller-owned ring memory; future ring-density work |
| `REGISTERED_FD_ONLY` | deferred | fd-less rings; requires NO_MMAP |
| `SQE128` | deferred (first big-SQE opcode) | ring-wide; MIXED preferred then |
| `CQE32` | deferred (same) | ring-wide; MIXED preferred then |
| `SQE_MIXED` | deferred (same) | per-entry 128B SQEs |
| `CQE_MIXED` | deferred (same) | per-entry 32B CQEs via `IORING_CQE_F_32` in reap |

## Files

- `src/ring/`
- `src/syscall.h`
- `meson.build`
- `test/ring.c`

## Acceptance

A probe creates a ring with the exact required setup flags, submits one
`IORING_OP_NOP`, and reaps exactly one CQE with result zero and unchanged
nonzero `user_data`. Both architecture builds remain clean under the project
warnings and UBSan configuration; x86-64 behavioral execution is UC-012.

2026-08-30: done on aarch64 — the probe creates the ring with all five
flags, submits one NOP, and reaps exactly one CQE with result zero and the
user_data intact: exit 0 in an unconfined container (Docker's default
seccomp denies io_uring, exit 125) and `exitcode=0x00000000` in the VM.
Both architecture builds are clean; x86-64 behavioral runs are UC-012. The
surface came out as create/append_sqe/submit/reap: SQ_REWIND deleted the
SQ-side protocol, so "publish" never existed and submit owns the batch,
keeping it on a failed enter for a free retry.
