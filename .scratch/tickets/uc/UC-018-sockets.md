---
id: UC-018
title: Single-shot sockets
status: todo
depends: [UC-014]
---

## Goal

Bring the connection lifecycle — `socket`, `bind`, `listen`, `accept`,
`connect` — onto the ring as public libc calls, with the data plane running
through the `read`/`write` that already exist.

## Spec

Add `<sys/socket.h>` and `<netinet/in.h>`. `<netinet/in.h>` is uapi
re-export only (`struct sockaddr_in`, `IPPROTO_*` from `<linux/in.h>`) —
types and constants, no TU, no directory. `<sys/socket.h>` declares the five
calls plus the types they need.

**This ticket authors libuc's first ABI constants.** `AF_*` and `SOCK_*`
have no uapi source: they live in the kernel's internal
`include/linux/socket.h` and `include/linux/net.h`, which are not a
permitted include, and every libc defines them itself. Author only what the
calls consume, values verified against `out/src` in review; do the same for
`socklen_t` (`unsigned int`, the musl/glibc ABI; the kernel reads an `int`)
and the generic `struct sockaddr`. Landing this amends invariant 4's source
list with the third case: libc-authored ABI constants where uapi has none,
verified against the pinned tree rather than included. uapi supplies the
rest (`__kernel_sa_family_t`, `struct sockaddr_in`).

Each wrapper leaves a zeroed SQE template with UC-013's await operation.
Field mappings per the pinned prep functions (`out/src/io_uring/net.c`):

| libc call | ring operation |
|---|---|
| `socket(domain, type, protocol)` | `IORING_OP_SOCKET`: `fd`=domain, `off`=type, `len`=protocol |
| `bind(fd, addr, len)` | `IORING_OP_BIND`: `addr`=addr, `addr2`=len |
| `listen(fd, backlog)` | `IORING_OP_LISTEN`: `len`=backlog |
| `accept(fd, addr, lenp)` | `IORING_OP_ACCEPT`: `addr`=addr, `addr2`=lenp |
| `connect(fd, addr, len)` | `IORING_OP_CONNECT`: `addr`=addr, `addr2`=len |

Liveness differs by direction and the wrappers must not assume one rule:
`bind` and `connect` copy the sockaddr into kernel memory at prep
(`move_addr_to_kernel` in `io_bind_prep`/`io_connect_prep`,
`out/src/io_uring/net.c`) — consumed at submission; `accept`'s addr/lenp
are written at completion and the suspended frame keeps them live. `accept`
returns the new descriptor as the CQE result. `sqe->ioprio` stays zero:
that field carries `IORING_ACCEPT_MULTISHOT` (`io_accept_prep`), and this
ticket keeps `__libuc_fiber_await`'s exactly-one-CQE contract everywhere.

Multishot accept/recv, `send`/`recv` with their `MSG_*` constants,
`shutdown` (`REQ_F_FORCE_ASYNC` — the first deliberate io-wq operation),
and `SEND_ZC` with its notification are all out: multishot and zero-copy
belong to UC-016's operation records, and whichever of UC-016/UC-018 lands
second carries the joint network probes. The acceptance path is TCP
loopback only; no io-wq or no-punt claims are established for other
address families.

## Files

- `include/sys/socket.h`
- `include/netinet/in.h`
- `src/sys/socket/`
- `test/socket_echo.c`
- `meson.build`

## Acceptance

A probe uses public declarations for every call. A server fiber creates a
`SOCK_STREAM` socket through the ring, binds a fixed loopback port,
listens, parks in `accept`, echoes one fixed payload back through
`read`/`write`, and closes both descriptors. A client fiber connects,
writes the payload, reads the echo, and observes the exact bytes and
count. The loop returns empty with nothing in flight.

`connect` to an unbound loopback port returns `-1` with `ECONNREFUSED`;
the other fiber's seeded `errno` is unchanged, as is `errno` after
success. Authored `AF_*`/`SOCK_*` values match the pinned tree's internal
definitions at review.

Both architectures build cleanly under the project warnings and UBSan
configuration. The aarch64 probe passes in the unconfined container and
VM; x86-64 behavioral acceptance remains governed by UC-012.
