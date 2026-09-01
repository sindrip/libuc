# libuc — deliverable and compatibility evidence

Status: **current north-star record, 2026-08-30.** The retired runtime spike is
history. The top-level `src/`, `include/`, and `meson.build` are libuc.

## The deliverable

libuc is a static C library and startup environment for 64-bit little-endian
Linux. It does not link another libc or liburing. Its public C/POSIX calls hide
the scheduler: a call that may block prepares an io_uring operation, suspends
the current fiber, and returns after the completion using normal libc result
and `errno` conventions.

The eventual sysroot contains:

```text
lib/
  crt1.o
  crti.o
  crtn.o
  libc.a
  libm.a
  librt.a
  libpthread.a
  libdl.a
include/
  C and selected POSIX headers
```

Only `libc.a` and the first installed headers exist today. Probe startup is
linked from a private architecture-specific archive; installing a complete CRT
and sysroot is future work. Empty compatibility archives may satisfy historical
link lines only when the symbols they imply are genuinely supplied elsewhere.
`libm.a` cannot be empty once sqlite-class consumers arrive.

## Current startup and runtime

The installed-CRT destination follows the mechanism already exercised by the
probes:

```text
_start
  -> parse initial process metadata and PT_TLS
  -> make the initial task scheduler zero
  -> create and enqueue the root fiber
  -> run constructors and main on that fiber
  -> return to the scheduler stack for reclamation
  -> exit_group
```

The root fiber is not a special C execution mode. It uses the same context,
stack, thread-local block, request, and scheduler paths as later fibers.
Scheduler zero's control frame stays on the kernel-provided initial stack and
runs without a compiler-visible thread-local block installed.

The archive currently provides startup, auxiliary-vector storage and
`getauxval`, thread-local geometry and installation, per-fiber `errno`, fibers,
the single-CQE reactor, the four compiler-required memory functions, public
`pipe2`/`pipe`/`read`/`write`/`close`, and the single-shot socket connection
lifecycle over the ring. Installed public headers are `<errno.h>`, `<fcntl.h>`,
`<string.h>`, `<unistd.h>`, `<sys/auxv.h>`, `<sys/socket.h>`, `<netinet/in.h>`,
and `<sys/types.h>`.

## ABI layers

Keep three contracts visibly separate:

| layer | examples | result convention |
|---|---|---|
| raw kernel seam | `__libuc_sys_*` | Linux return value: `-errno` in `-1..-4095` |
| private suspension seam | `__libuc_fiber_await`, future iterator core | ring-mediated; may suspend; retains operation lifetime |
| public C/POSIX | `read`, `write`, `close`, `thrd_yield` | standard declarations, `-1`/sentinel plus per-fiber `errno` |

Public functions are real link-visible symbols, not macro aliases. Kernel UAPI
types and constants come from the pinned installed headers; public libc headers
must not expose private scheduler or ring structures.

The private I/O names use `await` because they are not direct-syscall wrappers.
They prepare an operation, suspend the current fiber, and resume it with a
completion. Temporary SQ capacity and ring batching never appear in the public
ABI.

## Pull-iterator extension

POSIX calls continue to yield one result per call. Repeated sources gain a
separate libuc extension API: typed `next` operations return item, end, or
error, and destruction synchronously cancels and drains unfinished kernel work.
The shared result vocabulary will live in `<uc/iterator.h>`; typed socket
iterators arrive in `<uc/socket.h>` with UC-021 and UC-022.

This is a semantic API, not an io_uring exposure. An accepted-connection
iterator initially uses bounded single-shot rearming; a borrowed receive
iterator uses provided-buffer credit. The private operation-record layer routes
multiple CQEs, while adapters define item ownership and cleanup. UC-023 makes
zero-copy send a one-result extension, not an iterator: its notification keeps
memory live until the kernel is finished.

## Per-fiber C thread-local state

Static executables use the local-exec TLS model. Each fiber receives a copy of
the executable's `PT_TLS` initialization image and an architecture-correct TCB;
the context transition installs that fiber's thread pointer. The private
current-fiber lookup reads the TCB word, so libuc does not need a process-global
current-fiber variable or an internal `_Thread_local` object.

Public `errno` is a compiler-visible `_Thread_local int`, so its address and
value follow the fiber. Private raw kernel wrappers continue returning negative
errors and never consult it; public descriptor wrappers translate at their
boundary.

Thread-pointer installation is unconditional at the switch site. Capability is
checked once before the scheduler begins: AArch64 can write `tpidr_el0`; x86-64
requires advertised FSGSBASE and currently lacks a behavioral test environment
on the development host (UC-012).

## Vendored C: measured compatibility evidence

Two hosted AArch64 experiments were run on 2026-08-18. Their scripts were not
committed, so these are design evidence rather than a reproducible benchmark.

An `nm` audit over 14 static archives counted true external symbols after
subtracting definitions supplied by the same archive:

- no raw `svc #0` appeared; their kernel surface was visible through libc
  imports and can be audited before linking;
- lz4 and brotli needed no libc symbols in the tested configurations;
- nine libraries together needed 151 distinct libc symbols: zlib, lz4, zstd,
  xz, brotli, expat, jansson, libpng, and sqlite;
- sqlite alone imported 21 math functions, so a real `libm` is required;
- real libraries imported pthreads rather than C11 `<threads.h>`: the zstd,
  xz, and sqlite union was 20 pthread symbols, 15 mutex/condition-variable
  operations;
- several imports have no io_uring opcode, including metadata and process
  operations. That is an unsupported surface until each direct-syscall
  addition is discussed in the invariant, not permission to add fallbacks.

A roughly 130-line hosted pthread shim mapped that 20-symbol subset to a
cooperative fiber scheduler and linked prebuilt musl archives:

| workload | result |
|---|---|
| zstd MT compression, workers 0 through 16 | correct roundtrip, no deadlock |
| sqlite, 100k inserts in one transaction | correct; required recursive mutexes |
| liblzma MT encoder | completed with timed wait conservatively implemented as an untimed wait |
| all three | 404,025 mutex acquisitions, zero observed contention |

Zero contention follows from the tested topology: fibers sharing a scheduler
cannot interleave inside a critical section that contains no suspension point.
It does not make mutex words safe between schedulers, and the shim did not test
library races that require parallel execution.

Throughput stayed at the single-threaded level as worker count rose. That proves
compatibility and low cooperative coordination overhead, not parallel speedup.
A library that creates threads for compute throughput must either be built
without that feature or use a future explicit parallel/offload design.

## Thread compatibility shape

For libuc's C11 surface, `thrd_t` is a fiber created on the calling scheduler.
It never migrates. A pthread compatibility layer maps the subset required by a
chosen vendored library onto the same machinery.

Consequences:

- spin waiting for another fiber on the same scheduler is a permanent hang;
- mutex and condition-variable wait queues are scheduler-local and need no
  atomics only while their objects remain scheduler-local;
- `thrd_yield` is a public wrapper over `__libuc_fiber_yield`;
- `thrd_sleep` uses `IORING_OP_TIMEOUT`;
- cross-scheduler parallelism is not smuggled into `thrd_create`;
- a library's allocation and free remain on one scheduler, preserving allocator
  ownership.

The exact object layouts, destructor passes, join/detach state, recursive mutex
semantics, and pthread ABI subset land only with the first vendored consumer.

## Work still larger than threads

### Allocation

Vendored C immediately needs `malloc`, `calloc`, `realloc`, `free`, and aligned
allocation. The allocator must preserve scheduler ownership, define the
fiber-versus-scheduler lifetime boundary, and retain kernel-visible allocations
until terminal operation events. Stack pooling in UC-011 is related memory
infrastructure, not a substitute for this allocator.

### Descriptor and pathname I/O

UC-014 established pipe/read/write/close on top of per-fiber `errno`. UC-018
adds the single-shot socket connection lifecycle. Filesystem metadata, path
operations, descriptor tables, registered resources, io-wq policy, and stdio
remain separate deliverables. An opcode's absence does not weaken the
direct-syscall registry.

### Formatted I/O

`vfprintf` is a format-language implementation plus the `FILE` layer. A useful
first tier covers integers, strings, characters, pointers, flags, width, and
precision. Correct decimal floating-point formatting is a separate large step.
Buffered output is a suspension point when it flushes through the ring; crash
and pre-ring diagnostics cannot depend on it.

## Outside the first complete static libc

Dynamic linking, `libc.so`, `dlopen`, transparent `fork`/`exec`, locales, and
wide-character completeness are not early deliverables. `setjmp`/`longjmp` is
likely to arrive earlier because vendored image and compression libraries
commonly import it.

## Open questions

- Which first vendored library defines the minimum public-header and pthread
  compatibility set?
- Does general allocation default to fiber arenas with an explicit long-lived
  scheduler allocator, or one scheduler allocator with scoped helpers?
- Which descriptor classes are accepted before the io-wq policy is settled?
- When does the build install a sysroot/CRT rather than only `libc.a` and
  headers?
- Is a compiler wrapper useful, or is a documented `--sysroot` link sufficient?
