---
id: UC-014
title: Expose ring-backed descriptor I/O as libc
status: done
depends: [UC-013]
---

## Goal

Make `pipe`, `read`, `write`, and `close` the first public libc calls whose
blocking behavior suspends the calling fiber on the ring.

## Spec

Add `<sys/types.h>`, `<unistd.h>`, `<errno.h>`, and `<fcntl.h>` with the types,
declarations, and pinned UAPI constants these calls need. `errno` is a
per-fiber `_Thread_local int` lvalue. Kernel constants are included, never
retyped.

Staged prerequisite, already landed: `<errno.h>` and the compiler-visible
per-fiber `errno` object. This ticket remains open until the descriptor calls,
the rest of their headers, and the acceptance probe land.

Each wrapper leaves a zeroed SQE template with UC-013's await operation; only
the scheduler copies it into the SQ and stamps `user_data`.

| libc call | ring operation |
|---|---|
| `pipe2(fds, flags)` | `IORING_OP_PIPE` |
| `pipe(fds)` | `pipe2(fds, 0)` |
| `read(fd, buf, count)` | `IORING_OP_READ` |
| `write(fd, buf, count)` | `IORING_OP_WRITE` |
| `close(fd)` | `IORING_OP_CLOSE` |

`read` and `write` use `sqe.off == -1` for the open-file position
(`out/src/io_uring/rw.c:479-493`). A `count` above `SSIZE_MAX` is rejected
with `EINVAL` before any transfer, mirroring the syscall path's
`rw_verify_area` (`out/src/fs/read_write.c`), which the ring path does not
apply. Below that, `sqe.len` is 32-bit while `size_t` is 64-bit, so a larger
`count` is explicitly clamped to `INT32_MAX`; the kernel's `import_ubuf` then
applies Linux's normal `MAX_RW_COUNT` ceiling
(`out/src/lib/iov_iter.c:1445-1450`). Together these match a direct Linux
read/write exactly — `EINVAL` and partial-transfer cases both — without
narrowing modulo 2^32 or retyping the kernel-private `MAX_RW_COUNT` constant.

A negative CQE result becomes `-1` plus per-fiber `errno`; success leaves
`errno` unchanged. The suspended call frame keeps the SQE and buffers live
through completion. As in ordinary C, aliases in other fibers are outside what
the type system can protect and must not access an in-flight destination.

One operation per fiber, the kernel fd table, and unregistered resources are
enough here. The acceptance path is deliberately pipe-only: pollable pipe I/O
does not establish a no-io-wq claim for arbitrary descriptors. The public
symbols retain their normal descriptor-generic signatures; regular-file and
`close` paths that may punt are measured and governed by a later io-wq ticket.
No fd registry, allocator, stdio, pathname operation, cancellation, multishot
operation, or direct syscall implementation lands.

## Files

- `include/sys/types.h`
- `include/unistd.h`
- `include/errno.h`
- `include/fcntl.h`
- `src/thread_local/`
- `src/errno/`
- `src/unistd/`
- `src/fiber/`
- `src/scheduler/`
- `test/libc_io.c`
- `meson.build`

## Acceptance

A probe uses public declarations for every I/O call. `pipe2` returns two
descriptors through the ring. A reader enqueued before a writer suspends in
`read` on the empty pipe, resumes only after the writer writes a fixed payload,
observes the exact bytes and count, and closes both descriptors through the
ring. The loop returns empty with nothing in flight.

An invalid descriptor returns `-1` with `EBADF`; a second fiber's seeded
`errno` remains unchanged, as does `errno` after success. No public call
returns raw `-errno`.

A `count` greater than `INT32_MAX` is prepared with `sqe.len == INT32_MAX`, not
the low 32 bits of `count`; this is checked at the preparation seam without
requiring an allocation of that size. A `count` above `SSIZE_MAX` returns `-1`
with `EINVAL` and no transfer.

Both architectures build cleanly under the project warnings and UBSan
configuration. The aarch64 probe passes in the unconfined container and VM;
x86-64 behavioral acceptance remains governed by UC-012.
