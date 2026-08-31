# libuc — current foundation and roadmap

Status: **current design record, 2026-08-31.** Ticket status and acceptance live
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
       -> main returns: publish PROCESS_EXIT with its value
  -> the reactor stops and exit_group carries that status
```

Returning from `main` is `exit` (C11 5.1.2.2.3): the reactor stops and every
other fiber is abandoned to `exit_group`. A `main` that ends with `thrd_exit`
instead leaves a zombie; the reactor drains and the program exits
`EXIT_SUCCESS` after the last thread terminates (7.26.5.5).

The landed layers are:

| layer | current contract |
|---|---|
| thread-local image | one immutable `PT_TLS` description decoded from program headers; absence is valid |
| thread-local block | one mapping per fiber, ABI-specific placement, common two-word TCB |
| fiber | record carved from the top of its own unguarded stack mapping, saved callee context, per-fiber TCB |
| ring | `SINGLE_ISSUER | DEFER_TASKRUN | NO_SQARRAY | SQ_REWIND | SUBMIT_ALL`; CQ acquire/release; short-submit retry |
| scheduler | caller-owned scheduler zero, intrusive FIFO, `ready`/`parked` structural counts; owns the request protocol a fiber raises |
| reactor | one SQE and exactly one CQE per await; a request lives on the asking fiber's frame and the switch carries its address; CQE `user_data` is that request pointer |
| fairness | one ready generation per reactor iteration; pure yield generations do not enter the kernel |
| public `errno` | compiler-visible `_Thread_local int`; therefore per-fiber after thread-pointer installation |
| threads | `<threads.h>` create, current, equal, yield, exit, join, and detach over fiber requests; a join parks in the target's single joiner slot, and a record outlives its exit until joined or detached |
| descriptor I/O | public `pipe2`, `pipe`, `read`, `write`, and `close`; completion errors translate to `errno` |

The single-CQE restriction is enforced, not implicit: the current reap path
traps on `IORING_CQE_F_MORE`, `IORING_CQE_F_NOTIF`, and `IORING_CQE_F_SKIP`.
UC-020 replaces that boundary with operation records and a private pull-
iterator engine.

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

1. **UC-011 — scheduler-owned stack/block recycling.** It makes the scheduler
   ownership seam explicit, then measures separate versus carved TLS storage.
2. **UC-019 — completion-loss detection.** It maps and checks the kernel's
   dropped-CQE evidence while preserving the current single-CQE admission proof.
3. **UC-020 — pull iterators over operation records.** The completion key
   becomes generation + operation slot + tag. The private engine proves
   repeated delivery with multishot poll while fused `await` keeps its current
   performance.
4. **UC-017 — automatic iterator teardown and zombies.** Fiber exit cancels
   owned operations and delays all reclamation until their terminal events
   arrive.
5. **UC-021 and UC-022 — typed socket iterators.** Accepted connections use
   bounded single-shot rearming; receive chunks use provided-buffer credit.
   Neither API promises a particular io_uring opcode.
6. **UC-023 — zero-copy send lifetime.** One send remains a one-result API;
   its notification delays buffer reuse rather than becoming an iterator item.

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

A fiber pointer works only while a fiber has one single-shot operation. UC-020
replaces it with an operation record with a generation-bearing slot. Its bit
allocation, table growth, terminal protocol, and stale-key failure are shared
with the BPF-loop proposal. UC-019 separately establishes the ring-level rule:
an observed dropped CQE is fatal, while a full CQ with retained overflow entries
is not itself loss.

### Pull iterators

Operation records unify completion routing; typed pull iterators unify repeated
value delivery. Their common semantic protocol is `open`, `next -> item | end |
error`, and synchronous `destroy`. A terminal CQE may still yield the final
item; the iterator records local end after consuming it. `__libuc_fiber_await`
is the fused one-item equivalent, not an implementation assembled from three
iterator operations.

POSIX calls remain one-result calls. The extension namespace gains typed
accepted-connection and borrowed-receive iterators only with UC-021 and UC-022.
UC-023 keeps zero-copy send a one-result extension whose notification delays
buffer reuse. Multishot is an implementation strategy: unmetered accept/poll
cannot supply a hard finite delivery bound, so UC-021 begins with bounded
single-shot rearming; provided-buffer credit makes UC-022's receive bound
enforceable.

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
