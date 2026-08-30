---
id: UC-008
title: Create the current task's ring
status: todo
depends: []
---

## Goal

Reintroduce the smallest io_uring object needed by a scheduler, independently
of fiber queue policy.

## Spec

Add hand-written ring setup and mappings using only the pinned kernel UAPI.
Setup uses `IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN |
IORING_SETUP_NO_SQARRAY`; `IORING_SETUP_SQPOLL` is never accepted. The task that
calls setup is the ring's owner and remains its sole submitter.

The initial interface acquires an SQE, publishes a batch, enters the ring, and
reaps a CQE. SQ and CQ head/tail communication uses the required acquire and
release operations. Ring geometry and offsets come from `struct
io_uring_params`; no kernel structure or constant is retyped. Submission
capacity follows the kernel-consumed SQ head rather than assuming every
published entry was consumed.

`io_uring_setup`, `io_uring_enter`, and the ring mappings use the sanctioned
direct syscall path because no io_uring opcode can create or drive a ring. No
liburing, registered resources, resize, cancellation, multishot operation, or
ring teardown is introduced. The probe's process lifetime owns this first
ring; hosted teardown remains later work.

## Files

- `src/ring/`
- `src/syscall.h`
- `meson.build`
- `test/ring.c`

## Acceptance

On both architectures, a probe creates a ring with the exact required setup
flags, submits one `IORING_OP_NOP`, and reaps exactly one CQE with result zero
and unchanged nonzero `user_data`. The two architecture builds remain clean
under the project warnings and UBSan configuration.
