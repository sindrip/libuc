---
id: UC-014
title: Expose ring-backed descriptor I/O as libc
status: todo
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
(`out/src/io_uring/rw.c:479-493`) and must not silently narrow `count` into
`sqe.len`. A negative CQE result becomes `-1` plus per-fiber `errno`; success
leaves `errno` unchanged. The suspended call frame keeps buffers live through
completion.

One operation per fiber, the kernel fd table, and unregistered resources are
enough here. No fd registry, allocator, stdio, pathname operation,
cancellation, multishot operation, or direct syscall implementation lands.

## Files

- `include/sys/types.h`
- `include/unistd.h`
- `include/errno.h`
- `include/fcntl.h`
- `src/thread_local/`
- `src/io/`
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

Both architectures build cleanly under the project warnings and UBSan
configuration. The aarch64 probe passes in the unconfined container and VM;
x86-64 behavioral acceptance remains governed by UC-012.
