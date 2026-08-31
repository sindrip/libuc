---
id: UC-022
title: Borrowed receive iterator
status: todo
depends: [UC-017, UC-018, UC-020, UC-021]
---

## Goal

Add a typed iterator of borrowed socket byte chunks, backed by provided-buffer
credit and a multishot receive operation.

## Spec

Extend `<uc/socket.h>` with `struct uc_recv_iter`, `struct uc_recv_item`, and
`uc_recv_iter_init`, `uc_recv_iter_next`, and `uc_recv_iter_destroy`. A receive
item is a read-only loan valid until the next call to `next` or iterator
destruction. Beginning the next call returns the preceding loan to the
provided-buffer pool; callers may not retain or mutate it afterward.

The iterator owns its buffer pool, registration, delivery queue, and
`IORING_OP_RECV` preparation for its entire lifetime. Multishot recv requires
buffer selection (`out/src/io_uring/net.c:841-846`). Do not enable incremental
buffer consumption or receive bundling in the first implementation: one
ordinary provided buffer authorizes at most one data CQE.

Credit includes terminal slack. Exhausting the provided buffers can itself
produce a terminal `-ENOBUFS` completion
(`out/src/io_uring/net.c:1245-1253,1283-1295`), so the invariant is:

```text
provided buffers + one terminal slot <= delivery queue capacity
```

A positive terminal CQE still yields its final borrowed item and makes the next
call return END locally. EOF returns END. A terminal negative CQE returns ERROR
with per-fiber `errno`, followed by END. Destroy returns any outstanding loan,
cancels and drains the receive, unregisters the buffers, and releases their
scheduler-owned storage only after the terminal event.

This is a byte-chunk iterator, not a message-boundary promise. Chunk boundaries
are determined by receive completions and buffer geometry. POSIX `read` remains
unchanged and `recv`/`send` with their standard one-result semantics are a
separate libc surface.

## Acceptance

- A loopback peer sends enough data for at least two buffers; `next` yields the
  exact byte sequence across multiple borrowed items.
- The first item's bytes remain stable until the following `next`, which returns
  its buffer as credit before waiting for another item.
- The number of provided buffers plus terminal slack never exceeds delivery
  capacity, and buffer exhaustion cannot overrun the queue.
- Peer EOF returns END. A positive terminal delivery is returned as ITEM once
  and followed by END; an error returns ERROR once and then END.
- Early destroy cancels and drains before unregistering or releasing any buffer.
- Both architectures build cleanly; AArch64 passes in the container and VM.
