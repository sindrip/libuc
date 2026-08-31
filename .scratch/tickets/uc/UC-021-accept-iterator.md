---
id: UC-021
title: Accepted-connection iterator
status: todo
depends: [UC-017, UC-018, UC-020]
---

## Goal

Add the first typed public pull iterator: a fiber-owned sequence of accepted
connections in the libuc extension namespace, while preserving POSIX
`accept()` unchanged.

## Spec

Install `<uc/socket.h>` with `struct uc_accept_iter`,
`struct uc_accept_item`, and `uc_accept_iter_init`, `uc_accept_iter_next`, and
`uc_accept_iter_destroy`. `next` returns the result vocabulary from
`<uc/iterator.h>`. An item owns one accepted descriptor and its peer address;
ownership transfers to the caller when ITEM is returned.

The initial iterator has one unit of credit: at most one accept is in flight or
buffered. Implement it by rearming single-shot `IORING_OP_ACCEPT`, not by
`IORING_ACCEPT_MULTISHOT`. The kernel multishot loop can post several accepted
descriptors before userspace runs (`out/src/io_uring/net.c:1699-1703`), so it
cannot implement a hard one-item bound. The API promises the bound, not an
opcode flag.

The operation record owns the address and length storage while an accept is in
flight; iterator initialization must not retain a pointer into a returned
caller frame. Add only the public address-storage ABI the item consumes and
verify any libc-authored layout or constants against the pinned tree.

`next` consumes a buffered connection and rearms before returning when the
iterator remains live. A terminal kernel error returns ERROR and ends the
iterator; its next call returns END locally. Destroy is idempotent, cancels and
drains an in-flight accept, and closes every accepted descriptor that was
buffered but never transferred, using `IORING_OP_CLOSE`.

The iterator is fixed to its creating fiber and scheduler. POSIX `accept`
continues to prepare one SQE, suspend, and return one descriptor; it is not
implemented through the iterator.

## Acceptance

- A loopback server iterator yields two typed connection items in order and
  rearms between them while POSIX `accept()` retains its existing behavior.
- At no point can more than one accepted descriptor be in flight or buffered
  for the iterator.
- Destroy before the next item cancels and drains the accept. Destroy with a
  buffered connection closes that descriptor before returning.
- A terminal error produces ERROR once and then local END; an item descriptor
  is never both caller-owned and cleanup-owned.
- The loop returns with no live operation, cancel request, or leaked descriptor.
- Both architectures build cleanly; AArch64 passes in the container and VM.
