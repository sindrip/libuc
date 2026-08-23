---
id: RT-008
title: io-wq tripwire — prove no kernel threads work on our behalf
status: todo
depends: [RT-005]
---

## Goal

Make the runtime's central claim checkable: *N threads, N rings, nothing else*
— where N is the number of **schedulers**, which a program chooses and which is
not derivable from the cpu count.
io_uring is not threadless, and the design depends on never triggering its
fallback thread pool.

## Spec

### The threat

When an operation cannot complete without blocking, io_uring punts it to
**io-wq**, a pool of kernel worker threads. These are not pinned and not
counted in your design. The classic trigger is a *buffered* file read that
misses page cache: `IOCB_NOWAIT` fails and the op is punted.

So "all I/O goes through the ring" can quietly mean "an unpinned kernel thread
pool does your I/O on whatever core it likes" — landing directly on top of the
pinned schedulers this architecture is built around.

### The design response

Never trigger it, and make an accidental trigger **loud**:

1. Register a deliberately tiny worker cap immediately after ring setup:

   ```c
   unsigned vals[2] = { 1, 1 };   // [0] bounded, [1] unbounded
   io_uring_register(fd, IORING_REGISTER_IOWQ_MAX_WORKERS, vals, 2);
   ```

   With sockets-only operation the pool stays dormant, so the cap costs nothing.
   If a punt path ever appears, it manifests as visible serialisation rather
   than invisible threads — a loud failure instead of a silent architectural
   leak.

2. Assert the thread count. At startup, after ring setup, count entries in
   `/proc/self/task` and compare against the **scheduler count** — 1 at
   milestone 1, whatever the program spawned at milestone 3. Compare against
   the scheduler registry, never against the cpu count: the two are unrelated
   by design, and a check written against cpus would fail on any program that
   oversubscribes or under-subscribes its cores. Report via `raw_write` and, in
   a debug build, halt on mismatch.

   Reading `/proc` requires `openat`/`read`/`close` — all of which have opcodes,
   so this goes **through the ring**, not via direct syscalls. It is also a
   useful early exercise of the file path.

### The escape hatch, if it ever fires

`IORING_REGISTER_IOWQ_AFF` still exists on 7.2 — verified at
`out/src/io_uring/register.c:860` and
`out/src/include/uapi/linux/io_uring.h:674`. It sets the affinity mask for io-wq
workers, so they can be confined to a core disjoint from the schedulers.

Do not reach for it pre-emptively. Using it concedes that a kernel thread pool
does part of the I/O, which weakens the project's defining claim from "solely
io_uring" to "io_uring plus a pool whose scheduling we don't control". It is a
mitigation for a diagnosed problem, not a default.

## Files

- `src/ring.c` — register the cap at setup
- `src/diag.c` — thread-count assertion
- `src/main.c` — call it after ring init

## Acceptance

- `IORING_REGISTER_IOWQ_MAX_WORKERS` returns success; prior values are logged.
- Thread count read from `/proc/self/task` **via ring opcodes** matches expected.
- No `iou-wrk-*` entries appear in `/proc/self/task` during a full milestone-1
  run.
- Independent cross-check that does not trust our own code: under `./debug.sh`,
  break after ring setup and inspect the kernel's view of the task list via
  `out/System.map` symbols. There is no shell to cross-check from — the runtime
  is PID 1 alone — so the debugger is the second opinion.

## Notes

Worker names are `iou-wrk-<pid>` (and `iou-sqp-<pid>` for SQPOLL, which must
never appear at all — this design forbids `SQPOLL`, verified at
`out/src/io_uring/io_uring.c:2815-2821`). Seeing either is a bug, not a tuning
opportunity.

This ticket becomes materially more important at milestone 2, when real file
I/O appears. Landing it now means the regression is caught the day it's
introduced rather than discovered during a later performance investigation.
