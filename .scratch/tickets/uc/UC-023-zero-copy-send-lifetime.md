---
id: UC-023
title: Zero-copy send lifetime
status: todo
depends: [UC-017, UC-018, UC-020, UC-021]
---

## Goal

Expose zero-copy send as a libuc extension with one result and an explicit
buffer-lifetime guarantee. It is not a pull iterator: the completion
notification completes the lifetime of the one send.

## Spec

Add `uc_send_zc(fd, buffer, count, flags)` to `<uc/socket.h>`. It returns the
send result using ordinary one-result conventions, but it does not return until
the caller's buffer is safe to reuse. This makes the lifetime rule part of the
API rather than an undocumented post-return loan. POSIX `send` (when added)
keeps its ordinary one-result, reusable-buffer semantics and is not silently
implemented with this operation.

Prepare `IORING_OP_SEND_ZC` with a primary completion key and a NOTIFICATION
key in `sqe->addr3`. Kernel preparation assigns the notification's
`user_data` from `addr3` and marks it `IORING_CQE_F_NOTIF`
(`out/src/io_uring/net.c:1388-1404`). The primary CQE has `F_MORE` while the
notification remains due (`out/src/io_uring/net.c:1598-1611`), including a
failed request that still requires cleanup (`out/src/io_uring/net.c:1614-1620`).
The record therefore stores the primary result, retains the caller buffer, and
only resumes its waiter after the matching notification has arrived.

The two keys name one record with distinct tags and the same slot and
generation. Reap validates both before dispatch; a notification cannot be
mistaken for another operation's result. The record retires only after both
the primary and notification protocol are complete. An early preparation
failure before notification allocation is an ordinary single terminal error;
the kernel reports notification-allocation failure from preparation
(`out/src/io_uring/net.c:1381-1397`).

Cancellation and owner death obey the existing cancel-and-drain rule. A cancel
CQE is not a permission to reuse the buffer: preserve the record and buffer
until the original protocol's terminal events are drained. UC-017's zombie
cleanup owns this path. Never turn the notification into an iterator item or
expose raw completion flags publicly.

## Acceptance

- A loopback zero-copy send returns the byte count only after its notification;
  the caller may reuse the source buffer immediately on return.
- A primary result with `F_MORE` does not wake or retire the operation before
  the matching NOTIFICATION CQE.
- A primary error that still carries `F_MORE` likewise retains the buffer until
  notification; a preparation failure without notification returns once.
- Different tag, stale-generation, cancel-first, and target-first CQE paths
  cannot reclaim the buffer or record early.
- POSIX one-result send behavior remains independent of `uc_send_zc`.
- Both architectures build cleanly; the AArch64 probe passes in the container
  and VM.
