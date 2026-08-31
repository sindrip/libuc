# libuc — current foundation and roadmap

Status: **current design record, 2026-08-30.** Ticket status and acceptance live
under `tickets/uc/`; this file records the cross-ticket shape and the order in
which the remaining work becomes meaningful. The retired RT spike is available
in git history and is not a second implementation plan.

## Fixed point

libuc is a freestanding C23 libc whose potentially blocking calls suspend the
calling fiber over io_uring. Linux 7.2 is the development target. PID 1 and the
minimal VM give the scheduler and ring mechanics a controlled machine; hosted
operation later turns the pinned version into a floor plus capability probing.

AGENTS.md owns the invariants. The consequences most relevant to sequencing are:

- a ring belongs to one Linux task, therefore one scheduler owns one ring;
- fibers never migrate and scheduler-owned memory is never freed elsewhere;
- fibers yield only through an explicit yield or a blocking runtime operation;
- kernel ABI definitions come from the pinned UAPI, never local replicas;
- operations with io_uring opcodes do not acquire direct-syscall fallbacks.

## What exists

The libuc archive and every probe build from the top-level Meson project on
AArch64 and x86-64.

Startup currently does this:

```text
_start
  -> parse argc/argv/envp and auxv
  -> record the executable PT_TLS image
  -> make the initial Linux task scheduler zero and create its ring
  -> create and enqueue the root fiber
  -> scheduler dispatches the root fiber
       -> install its thread pointer
       -> run constructors
       -> call main
       -> publish EXIT
  -> destroy the root fiber from the scheduler stack
  -> exit_group(main status)
```

The landed layers are:

| layer | current contract |
|---|---|
| thread-local image | one immutable `PT_TLS` description decoded from program headers; absence is valid |
| thread-local block | one mapping per fiber, ABI-specific placement, common two-word TCB |
| fiber | caller-owned record, caller-sized unguarded stack mapping, saved callee context, per-fiber TCB |
| ring | `SINGLE_ISSUER | DEFER_TASKRUN | NO_SQARRAY | SQ_REWIND | SUBMIT_ALL`; CQ acquire/release; short-submit retry |
| scheduler | caller-owned scheduler zero, intrusive FIFO, `ready`/`parked` structural counts |
| reactor | one SQE and exactly one CQE per await; CQE `user_data` is the parked fiber pointer |
| fairness | one ready generation per reactor iteration; pure yield generations do not enter the kernel |
| public `errno` | compiler-visible `_Thread_local int`; therefore per-fiber after thread-pointer installation |
| descriptor I/O | public `pipe2`, `pipe`, `read`, `write`, and `close`; completion errors translate to `errno` |

The single-CQE restriction is enforced, not implicit: the current reap path
traps on `IORING_CQE_F_MORE`, `IORING_CQE_F_NOTIF`, and `IORING_CQE_F_SKIP`.
UC-016 replaces that boundary with operation records.

## Why the ring has these flags

- `SINGLE_ISSUER` installs scheduler/task ownership in the kernel. Setup stores
  `current` as `submitter_task` (`out/src/io_uring/io_uring.c:3065-3067`) and
  submission rejects another task (`out/src/io_uring/tctx.c:198-204`).
- `DEFER_TASKRUN` keeps completion task work on explicit entries by the owning
  task. It is also the mode required by the optional BPF loop.
- `NO_SQARRAY` removes an indirection and the only invalid-index dropped-SQE
  path (`out/src/io_uring/io_uring.c:1990-2003`).
- `SQ_REWIND` makes each enter consume a batch from slot zero and resets the
  kernel's cached head after the batch
  (`out/src/io_uring/io_uring.c:1966-1979,2022-2034`).
- `SUBMIT_ALL` continues after per-SQE preparation errors. Request-allocation
  shorts still exist (`out/src/io_uring/io_uring.c:2042-2068`), so libuc moves
  the unsubmitted suffix to slot zero and retries.

`SQPOLL` remains impossible: the kernel rejects it with `DEFER_TASKRUN`
(`out/src/io_uring/io_uring.c:2815-2821`).

## Active order

The ticket index is `tickets/README.md`. The useful dependency order is:

1. **UC-018 — single-shot sockets.** Add the public connection lifecycle over
   the ring and exercise it with a loopback echo. This remains within the
   exactly-one-CQE reactor contract; multishot receive and accept wait for
   UC-016.
2. **UC-011 — scheduler-owned stack/block recycling.** Independent of sockets,
   but required before a public fiber-spawn surface or large fiber counts. It
   first makes the scheduler ownership seam explicit, then measures separate
   versus carved TLS storage.
3. **UC-016 — operation records and multi-CQE delivery.** The completion key
   becomes generation + operation slot + tag. This is where streams, bounded
   delivery, terminal-slot recycling, and manual cancel-and-drain land.
4. **UC-017 — automatic cancellation and zombies.** Fiber exit cancels owned
   operations and delays all reclamation until their terminal events arrive.

UC-012 is not implementation work; it restores behavioral acceptance when a
real FSGSBASE-capable x86-64 environment is available. Until then x86-64 is a
compile/link tier and AArch64 carries behavioral execution.

## Decisions deferred on evidence

### io-wq

Pipe and socket operations can use internal async polling. Generic file I/O and
some close paths can punt to io-wq. Before libuc claims arbitrary descriptor
I/O preserves the shared-nothing performance model, add a forcing probe, record
which opcodes/file types punt in 7.2, and choose a policy: reject, constrain,
or configure and isolate the worker pool. Do not infer the answer from socket
acceptance.

### Fiber storage

Today's fiber performs two mappings: its caller-sized stack and its
thread-local block. UC-011 decides whether one scheduler-owned pool absorbs
both. The choice needs create/recycle syscall counts, VMA counts, guard-page
cost, and a teardown policy; `stacks.md` is the input record.

### Completion identity

A fiber pointer works only while a fiber has one single-shot operation. The
next identity is an operation record with a generation-bearing slot. The bit
allocation, table growth, delivery capacity, overflow policy, and stale-key
failure are UC-016 decisions. `scheduler.md` records the lifetime rules and
`bpf-loop.md` must use the same operation address space.

### More schedulers

Additional schedulers require `clone`, placement, a registry or other explicit
addressing, scheduler-local storage, and a cross-scheduler doorbell. They do not
permit fiber migration. `transport.md` is a proposal for message delivery, not
an active implementation spec.

### The rest of libc

An allocator, pthread/C11 thread compatibility, pathname/filesystem calls,
stdio, process APIs, and vendored static libraries remain libuc deliverables.
They land when a concrete C consumer needs them. `libuc.md` records the measured
compatibility evidence and the intended ABI layers.

## Verification

For every documentation-driven implementation change:

```sh
meson compile -C .cache/meson-aarch64
meson compile -C .cache/meson-x86_64
meson test -C .cache/meson-aarch64
meson test -C .cache/meson-x86_64
ninja -C .cache/meson-aarch64 clang-tidy
```

Run the affected AArch64 probes in an unconfined Linux container and in the VM.
When shared startup, thread-local, fiber, ring, scheduler, or linker code moves,
rerun every dependent probe rather than only the current ticket. The VM's
attempted-to-kill-init panic remains the expected envelope while probes return;
their status in `exitcode=` is the acceptance value.
