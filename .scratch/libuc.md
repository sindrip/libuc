# libuc — the minimal deliverable

Status: **conversation-derived, 2026-08-18.** Nothing here changes a current
ticket; RT-005 and RT-006 remain the next work. Scope: what a first libuc must
ship to compile, link, and run a C11-threads program against, and which
decisions determine that list.

## Where this sits

`src/` and the RT-00N tickets are a **viability probe**, and a way to learn C
properly. Their product is knowledge — the ticket Outcome sections and these
`.scratch/` notes — not a library anyone links against. If the architecture
holds, **libuc is the goal**, and it is a fresh tree seeded by *copying* from
`src/`: no shared build, no compatibility obligation, no abstraction inserted
today for the sake of reuse tomorrow.

Two consequences, both cutting against normal instincts:

- **`src/` owes libuc nothing but answers.** Do not add seams for reuse.
  strategy.md's arch-neutral TLS-layout interface is a libuc-era concern, not a
  context-switch concern today. The characteristic failure of a spike is
  refusing to throw it away, and every abstraction added for the successor's
  benefit is another reason not to.
- **Retrofit cost is not an argument for deciding anything early.** If libuc
  copies rather than depends, rewriting the context switch is free. What is not
  free is *not knowing whether the thing works*. So the items below earn their
  place now as **experiments**, never as layout commitments.

## What the probe has to answer

| question | what would answer it | status |
|---|---|---|
| Does the ring cover the ABI? | every op libuc needs has an opcode | largely answered — plan.md's verified list, extended here |
| Does io-wq stay out of it? | the tripwire | RT-008 |
| Is blocking-as-suspension actually cheap? | switch + ring round trip vs a syscall | **open** — not measured yet |
| Does per-fiber TLS work? | a guest `_Thread_local` resolving through `tpidr_el0` across a switch | **no ticket** — cheap, and the answer shapes the TCB |
| Can cooperative scheduling host foreign C at all? | vendor a static library and run it on a fiber | **answered — spiked, see below.** zstd/xz/sqlite run unmodified on a 20-symbol pthread shim |

The last row is the one that decides whether libuc is worth building. It is now
a finding rather than an assumption — see the next section.

## Spiked: measured, not assumed

Two experiments, 2026-08-18. Scripts lived outside the repo; nothing touched
`src/`.

### 1. What vendored C actually imports

`nm` over 14 real aarch64 static archives, counting *true* external symbols
(undefined minus defined elsewhere in the same archive — a raw `nm -u` count is
inflated several-fold by intra-archive references).

- **Zero raw syscalls.** Not one `svc #0` in any of the 14. Everything goes
  through libc, so `nm` sees a vendored library's entire kernel surface. A
  library can be audited completely before it is linked.
- **Spin-waiting is not a thing.** Zero `pthread_spin_lock` imports; one `yield`
  instruction across all 14 (libcurl). The cooperative-deadlock hazard that this
  page's earlier draft treated as the central risk is close to theoretical for
  this class of code.
- **151 distinct libc symbols** link nine real libraries: zlib, lz4, zstd, xz,
  brotli, expat, jansson, libpng, sqlite. lz4 and brotli need **zero** — they
  are freestanding already. zlib needs 19, expat 25, libpng 38 (including
  `setjmp`/`longjmp`), sqlite 86.
- **`libm.a` cannot be an empty archive.** sqlite alone imports 21 math
  functions. The empty-archive trick works for `librt`/`libdl`/`libpthread`
  only.
- **The ring does not cover POSIX filesystem metadata.** No opcode exists for
  `lseek`, `fcntl`, `access`, `readlink`, `utimes`, `fchmod`, `fchown`
  (checked against `out/src/include/uapi/linux/io_uring.h:255-320`); zlib wants
  2 of them, sqlite 9. Nor for `getrandom`/`getentropy`, which xz and expat
  want. Invariant 1 needs an explicit ruling: these are non-blocking metadata
  calls, so direct syscalls are defensible, but that is an addition to the
  permitted list rather than something to discover later.
- **`<threads.h>` is the wrong compatibility target.** Nothing imports
  `thrd_create`. Real C imports pthreads — zstd 11 symbols, xz 15, sqlite 10.
  The union across all three is **20 symbols**, and 15 of those are mutex and
  condvar. C11 threads remains the cleaner API to *offer*; pthreads is the one
  that links vendored code, and both map onto `task.c` identically.

### 2. The pthread shim, spiked

A ~130-line shim of those 20 symbols over a minimal fiber scheduler, linked
against the **prebuilt musl-compiled** `libzstd.a`, `liblzma.a`, and
`libsqlite3.a` — deliberately the harder ABI case, since those archives fixed
their `pthread_mutex_t` size at their own compile time. Hosted aarch64 Linux,
not freestanding: the question under test is semantic, and the ring is not
needed to answer it.

| test | result |
|---|---|
| zstd MT compression, 64 MB, workers 0→16 | correct roundtrip at every worker count, no deadlock |
| sqlite, 100k inserts in a transaction | correct, 0.011 s — **needs `PTHREAD_MUTEX_RECURSIVE`** |
| liblzma MT encoder, `timeout=300ms` | completes correctly *even with `cond_timedwait` stubbed as an untimed wait* — the timeout is an optimization, not a liveness requirement |
| 404,025 mutex acquisitions across all tests | **0 contended** |

**Zero contention is structural, not luck.** A critical section containing no
yield point cannot be contended under cooperative scheduling: the holder runs to
completion before anything else observes the lock. That is the empirical form of
"mutexes need no atomics" — every one of those 404k acquisitions was a load and
a store.

Its dark side is the same fact: **the shim hides the library's own races.** Their
locking correctness is never exercised, so a latent bug surfaces only on a future
preemptive or multi-core-per-process port, attached to an unrelated change.

**Throughput, aarch64 VM, 64 MB, zstd level 6.**

| workers | native pthreads | on fibers |
|---|---|---|
| 0 | 115 MB/s | 120 MB/s |
| 2 | 233 MB/s | 120 MB/s |
| 4 | 412 MB/s | 120 MB/s |
| 8 | 529 MB/s | 119 MB/s |
| 16 | 512 MB/s | 118 MB/s |

Flat, which is exactly right: concurrency without parallelism. The important
half is that it is flat rather than *degrading* — 16 fibers of coordination cost
2% against the single-threaded path, so the shim is free and the only thing lost
is the speedup. Context switch measured at ~6 ns (12 ns per `rt_fiber_yield` round
trip), against roughly a microsecond for a futex-mediated thread wake.

**The deadlock detector works, and earned its keep immediately.** The first run
hung: the shim's `pthread_mutex_init` ignored its attr argument, silently
dropping `PTHREAD_MUTEX_RECURSIVE`, and sqlite self-deadlocked re-entering its
own mutex. Because the scheduler holds the whole state, "run queue empty and
some fiber still blocked" is exact global deadlock — it named the fiber and what
it blocked on. Real pthreads cannot tell you that; here it is a scheduler
invariant, and it turned the scariest failure mode of a shim into its most
precise diagnostic.

### 3. How many schedulers, and where

An attempt to measure this **failed, and the failure is the finding.** Recorded
because the first version of it looked convincing and was wrong.

Same total work (N x 64 MB, zstd level 6), three topologies, best of 3, N=8:

| topology | MB/s |
|---|---|
| A: one process, zstd `nbWorkers=8`, jobs sequential | 463 |
| B: 8 processes pinned one per core, `nbWorkers=0`, on fibers | 534 |
| C: 8 **real pthreads** pinned one per core, `nbWorkers=0` | 566 |

The first run compared only A and B, showed B ahead by 22%, and was written up
as evidence for thread-per-core. Adding C killed it: **real threads shard just
as well** — slightly better. The gap is between *independent jobs* and *zstd's
own multithreaded compressor*, which is a known property of block-parallel
compression libraries, and says nothing about fibers, cooperative scheduling, or
shared-nothing.

Two further defects in the original, both worth remembering as a shape:

- **The fibers were decoration.** Each process in B ran exactly one fiber at
  `nbWorkers=0`. Removing the scheduler entirely would not have moved the
  number. The row was labelled "per-core schedulers" and did not measure one.
- **The variance exceeded the effect.** Identical configuration across runs: B
  moved 602 -> 534 MB/s, A moved 493 -> 463. A 13% swing supporting a claimed
  22% difference. A claim that size needed repeated runs and error bars; it
  had one run of each.

The thread-per-core performance question is therefore **still open**: it needs
a benchmark run enough times to separate the effect from the noise. What
follows below never depended on the measurement.

**What forces the design is the allocator, not the scheduler.** Vendored C
mallocs on one thread and frees on another as a matter of routine. If
`pthread_create` distributed fibers across schedulers, a per-scheduler arena would be
violated on the first `free` — invariant 3, immediately. So `pthread_create`
must spawn on the calling scheduler, and that is not a scheduling preference but a
consequence of owning the allocator. The shim results in section 2 then carry
over intact: each core is exactly the single-core system that was spiked,
replicated N times, with mutexes still intra-core, still atomic-free, still
uncontended — because nothing about a mutex changes when the core next to it
gets its own scheduler.

`pthread_create` therefore becomes a **sharding primitive, not a parallelism
primitive** — Seastar's and TigerBeetle's model. For server-shaped work, where
parallelism comes from many independent connections, that is the natural shape
rather than a concession; whether it is also *faster* is the open question
above, not something this page has shown. What it plainly costs is the single
batch job that wants every core: zstd at `nbWorkers=8` gets one core's
throughput no matter how many cores exist. That is plan.md's open "offload path"
question, and it applies to any vendored library that threads for speedup.

**The casualty is the deadlock detector.** "Run queue empty and some fiber still
blocked" is exact with one scheduler and merely *local* with N — core A can be
idle with blocked fibers while core B is about to signal them. Global deadlock
becomes a distributed quiescence problem. The design carries no cross-scheduler
liveness machinery (a tick-based watchdog was considered and dropped), so
nothing exists to carry an "idle with blocked fibers" bit — the best diagnostic
property of the single-core design is the first thing multi-core takes away,
and it is currently rebuilt by nothing. If the gap ever needs closing, that is
a deliberate design job, not a revival by default.

If cross-scheduler pthreads are ever wanted anyway, `IORING_OP_FUTEX_WAIT`/`WAKE`
(`io_uring.h:307-309`) makes them ring-native: a fiber blocks on a futex without
blocking its core. Implementable and elegant — and it drags in atomics on the
mutex path, a cross-scheduler-safe allocator, two mutex implementations chosen
dynamically, and the loss of the atomic-critical-section property. That is the
whole complexity the design exists to avoid, for a workload it does not target.

### What this changes

- The foreign-C row in the probe table above is **answered for the compute and
  storage class of library**, which is the class libuc actually wants. It is
  *not* answered for I/O libraries, and does not need to be: libuv (228 external
  symbols — `fork`, `execvp`, `dlopen`, `epoll_*`, `inotify`, `signal`,
  `prctl`), c-ares, and curl want to own the event loop. You do not vendor an
  event loop into a runtime that is one.
- Threading can also simply be **compiled out** — `SQLITE_THREADSAFE=0`,
  undefined `ZSTD_MULTITHREAD`, xz's `--disable-threads` — which erases all 20
  symbols. The shim is therefore not required to make these libraries *work*; it
  is required to make them work **unmodified**, which is the stronger and more
  useful claim.

## The decision that determines the file list

**`thrd_t` is a fiber, not a cloned thread.** `thrd_create` spawns a task on the
calling scheduler; the thread never migrates. This is the only mapping consistent
with invariant 3, and it is the libuc thesis stated as an API: the guest calls
`mtx_lock` and receives a suspension rather than a futex.

C11 permits it outright — nothing in the standard promises parallel execution,
only concurrent progress. Three consequences, all of which are design and not
implementation:

- **A guest spin-wait hangs the core permanently.** `while (!atomic_load(&f));`
  reaches no yield point, and invariant 7 forbids the fix. The vendoring policy
  falls out of this: libuc hosts C that *blocks on I/O*, not C that spins. State
  this before vendoring anything, not after a hang.
- **Mutexes need no atomics.** Every contender for an `mtx_t` is a fiber on
  the same **scheduler**, and a fiber is never preempted by its own scheduler
  nor migrated to another, so the whole critical section runs with nothing else
  able to reach the word. Uncontended lock is a load and a store; contended is
  a push onto a wait queue and a switch. This is a win the shared-nothing model
  hands us, not a compromise it forces.

  Same-scheduler is the load-bearing half, and it is worth stating separately
  from same-core now that they can differ: two schedulers sharing a cpu are
  preempted against each other by the kernel, and if an `mtx_t` were reachable
  from both, none of the above would hold. Nothing makes one reachable — a
  `thrd_t` is a fiber on the calling scheduler — but the property comes from
  that, not from the topology.
- **Cross-core `thrd_create` is deliberately not in v1.** The ring-native
  primitive exists if it is ever wanted — `IORING_OP_FUTEX_WAIT`/`WAKE`/`WAITV`,
  verified at `out/src/include/uapi/linux/io_uring.h:307-309` — but a shared
  futex word is cross-scheduler mutable state, which is the thing invariant 3 exists
  to forbid. If parallelism is wanted later it arrives as `spawn(pin: core)`
  with message passing (transport.md), not as a portable-looking `thrd_create`
  that quietly violates the model.

## What we ship

```
out/libuc/
  lib/
    crt1.o                _start; auxv; TLS image; rt_boot; main on fiber 0
    crti.o  crtn.o        .init/.fini halves — vestigial under .init_array,
                          ~10 lines each, shipped only so an unmodified
                          `clang --sysroot=...` link succeeds
    libc.a                everything below
    librt.a libpthread.a libdl.a
                          empty archives (musl's trick), so a vendored
                          library's `-lpthread` does not fail the link
    libm.a                NOT empty — sqlite alone imports 21 math functions
  include/
    threads.h stdlib.h string.h stdio.h errno.h time.h assert.h ...
  ucc                     wrapper: clang --sysroot=... -static -nostdinc
```

`crtbegin.o` / `crtend.o` and `libclang_rt.builtins-aarch64.a` come from the
**compiler**, not from us. The builtins archive is a genuine link-line
dependency — `__udivti3` and friends — and is freestanding-safe, containing no
libc references of its own. Naming it here so it is not discovered as a link
error.

## What builds it

```
src/libuc/
  start/    crt1.S · __libc_start_main.c · exit.c (atexit, fini_array)
  tls/      tls.c (PT_TLS image -> per-fiber TCB) · __errno_location.c
  thread/   thrd.c mtx.c cnd.c tss.c once.c      <- thin over task.c
  mem/      malloc.c (per-scheduler arena) · string.c (exists)
  io/       fd table · read/write/openat/close as ring ops · stdio.c
  misc/     abort.c assert.c stack_chk.c · ubsan handlers (exist)
```

## Symbol layers and names

The probe and libuc surfaces name different contracts. Keep that distinction
visible rather than making today's functions look source-compatible before they
are ABI-compatible.

| layer | examples | contract |
|---|---|---|
| probe/runtime | `rt_socket`, `rt_close`, `rt_fiber_yield` | Raw `-errno`, task-only where applicable, and submission-capacity details may still be visible. |
| private libuc | `__libuc_await_socket`, `__libuc_await_close`, `__libuc_fiber_yield` | Reserved implementation namespace; ring-mediated suspension and scheduler mechanics. |
| public C/POSIX | `socket`, `close`, `read`, `write`, `thrd_yield` | Standard declarations, types, return conventions, and per-fiber `errno`. |

`await` is deliberate in the private I/O names: these are not thin direct
syscall veneers. They submit to the owning ring, suspend the current fiber, and
resume it with the completion. Calling them `__libuc_sys_*` would hide the most
important part of their semantics.

The public function is a real link-visible symbol, not a macro alias. Its thin
wrapper translates a negative kernel result into `-1` plus per-fiber `errno`;
the private layer absorbs temporary SQ-capacity backpressure so callers see the
operation's result rather than an internal queue state. The first static libuc
does not need glibc-style weak aliases: direct definitions are simpler until
aliases solve a concrete compatibility problem.

There is no `yield` keyword or coroutine facility in C23, nor in the current
C2y draft (the likely basis of C29). The standard spelling already exists as
`void thrd_yield(void)` in `<threads.h>` (ISO/IEC 9899:2024 7.28.5.8). That maps
cleanly because a libuc `thrd_t` is a fiber:

```c
void thrd_yield(void)
{
    __libuc_fiber_yield();
}
```

Keep `rt_fiber_yield()` while this is still the probe — spelled for the fiber
now that "task" and "fiber" no longer both name it. At the libuc boundary make
`thrd_yield()` the public ABI and keep the fiber primitive private. A custom
bare `yield()` adds a non-standard surface, while `sched_yield()` describes the
kernel thread rather than the fiber and would violate the ring/direct-syscall
boundary if used as the primitive.

## Ownership made structural

The Rust viability port exposed three contracts that libuc's C interfaces
must encode instead of leaving them as comments:

- **Submission accounting follows `sq_head`, not the last published
  `sq_tail`.** The kernel can consume a positive prefix of a submitted batch
  and return before waiting (`io_uring.c:2046-2068, 2646-2650`). Outstanding
  work is therefore `cached_sq_tail - sq_head`; a short batch is retried from
  that suffix. The probe scheduler deliberately panics on `ret != staged`
  until it owns that recovery state machine.
- **An SQE pointer is a staging-only capability.** The Rust ring makes the
  mutable SQE borrow end before submission and returns CQEs by value. libuc's
  public C ring surface should preserve the same lifetime shape: operation
  preparers fill an SQE inside the staging call, and no raw pointer into the SQ
  mapping escapes publication. The existing probe's `rt_ring_sqe()` is not the
  successor API.
- **The scheduler owns task addresses.** Rust's `Pin` states the requirement
  directly; libuc gets the same guarantee from its scheduler-owned task slab. A
  raw task pointer or slab offset may cross the switch and the kernel only after
  the scheduler has placed it at its final address. Caller-owned task structs
  are probe-era convention, not a libuc lifetime model.

Here, "slab" means stable, indexed allocation, not a small fixed-capacity array.
For millions of fibers, each core can grow task-record storage in `mmap`-backed
chunks, with objects never moved and freed slots returned to per-scheduler free
lists. A large reserved virtual range committed incrementally is another
possible implementation. Task records and task stacks are separate allocation
problems; stack density and guarding are covered in `stacks.md`.

A completion identity can eventually encode generation plus chunk/slot and an
operation tag. The generation detects stale completions, but it does not weaken
the lifetime rule: a slot cannot be recycled until its task has no operations
in flight. The BPF loop's verifier-friendly `param_region` is a separate bounded
addressing problem. It may require multiple regions, a pre-reserved region, or a
bounded BPF-visible active set; its current offset encoding should not silently
become a global limit on the number of userspace fibers.

## crt1 is where the inversion lives

A normal crt1 runs `_start` -> `__libc_start_main(main, ...)` -> `main`, with
`main` on the kernel-supplied stack. libuc's runs:

```
_start -> __libc_start_main -> rt_boot()          ring, core, arena
                            -> spawn fiber(main)
                            -> scheduler loop     never returns
```

**`main` runs on a fiber**, and the kernel-supplied stack becomes the
scheduler's. That single change is what makes every blocking call in the guest a
suspension, and it is why libuc is a platform rather than a shim — Foreactor
intercepts calls, libuc owns the process from the first instruction
(strategy.md, prior art).

`src/start.S` is already most of crt1. What it gains: auxv parsing (`AT_PAGESZ`,
and `AT_RANDOM` to seed the stack guard), `.preinit_array`/`.init_array`
traversal via the linker-provided `__init_array_start`/`__init_array_end`, and
`exit()` wiring so `.fini_array` and `atexit` handlers run — including the
`stdout` flush, without which a guest's output is lost on exit.

**RT-006's `rt_main` is crt1's future shape.** Worth writing it that way now: a
boot function, a spawn, and a scheduler loop that never returns, rather than a
milestone driver with the boot inlined.

## The boot contract

Status: conversation-derived, 2026-08-22. The language-side statement is
language.md §7 — init creates exactly one scheduler, every additional one is
program text, `main` runs on scheduler 0. This section is the libc-side
mechanics: what crt1's sequence above actually rests on, and the boot topology
that was considered and rejected.

**The scheduler is never created; the boot thread becomes it.** A fiber is a
manufactured thread of control — `rt_fiber_create` mmaps a stack, places a
guard page, and forges a context so the first switch lands in the trampoline.
The scheduler needs none of that. `struct rt_scheduler`'s own `context`
(`scheduler.h:62`) is never primed; it is written for the first time by the
switch *away* from it (`rt_scheduler_resume`, `scheduler.c:82`), so the
scheduler's context is captured lazily by the act of leaving it. Its state —
ring, ready queue, arena — is a struct the boot thread fills in before
anything runs. So there is no chicken-and-egg between fiber and scheduler: the
scheduler is presupposed by the process existing, and a scheduler with zero
fibers is valid — `rt_main` boots in exactly that order (`rt_scheduler_init`,
then `rt_scheduler_spawn`, then `rt_scheduler_run`).

**This is now structural, not just the boot path's habit.** There is no call
that creates a scheduler on another thread's behalf: `rt_scheduler_init` runs
on the thread that is becoming one, and the public surface is `become`, not
`create` (`.scratch/scheduler.md`). Boot is not a special case that the rest
of the design works around — it is the only case, replicated.

Boot is four steps, all on the kernel-supplied stack:

1. `_start` — neither fiber nor scheduler; just the process.
2. `rt_boot()` — fill in scheduler 0's state. The calling thread has now
   *become* the scheduler.
3. `rt_fiber_create(fiber 0)` — a stack and a primed context in the queue;
   nothing running yet but the boot thread.
4. Enter the loop. The first `rt_switch` stamps the scheduler's `context`,
   and from that instruction the kernel stack is permanently its home.

**Fiber 0 is the runtime's identity.** It is not mechanically special — just
the first thing the boot path spawns. In v1 its body is the crt1 wrapper: run
`main(argc, argv, envp)`, then drive `exit()`. If a supervising root is ever
wanted (language.md's `restart: OnCrash`), fiber 0's body becomes the
supervisor and `main` its first child — same machinery, one more
`rt_fiber_create`. The hierarchy above `main` is a fiber, never a scheduler.

**Rejected: a boot-time runtime/work scheduler split** — a tiny scheduler for
the runtime that spawns a work scheduler for the program. Four reasons, three
of them decisions already made elsewhere:

- **Nothing for it to do.** Zero ambient concurrency (language.md §7) means
  there is no job category for a dedicated runtime scheduler: reaping,
  resuming, timer expiry and the deadlock check all run inline in the
  scheduler loop, which gets the CPU at every suspension point.
- **A supervisor scheduler cannot supervise.** Invariant 7 forbids preempting
  a wedged core, the kernel vetoes migrating fibers off it (`SINGLE_ISSUER`
  binds a ring to its thread), and invariant 3 forbids the shared state that
  even *observing* it would need. Cross-core liveness machinery was considered
  and dropped (§3 above; plan.md milestone 3 accepts the wedged core as a bug
  class for the debugger).
- **It forces the open problems into boot.** Getting `main` onto a spawned
  scheduler needs clone, a second ring, and cross-scheduler transport — the
  unresolved question — before the first line of the program. "The runtime
  kernel is finished at single-core" exists precisely so nothing before `main`
  depends on transport.
- **It spends the exact deadlock detector on every program**, including those
  that never wanted topology; and on the 1-vCPU dev VM it is two threads
  timesharing one core.

The cost asymmetry is the argument in one line: a fiber is a 64 KB stack plus
a 168-byte context; a scheduler is a cloned thread, a ring with two mappings,
a crash altstack, an arena, and an unsolved transport problem. The runtime's
identity lives in the thing that costs a struct, not the thing that costs a
core.

**The legitimate form of "a scheduler for the work" is inverted.** The one
workload that genuinely wants another scheduler is CPU-bound batch work that
would starve a cooperative core — plan.md's open offload path. There the
*exceptional* work moves out, on demand, by program text
(`uc::rt::scheduler(cpu)` then `s.spawn(f, x)`), while `main` and the
latency-sensitive work stay on scheduler 0. Boot stays single; the split is a
decision the program makes when it has a reason, not a posture the runtime
assumes for it.

## TLS — one instruction, and errno falls out

Under `-static`, `_Thread_local` relaxes to the **local-exec** model: the
compiler emits `mrs xN, tpidr_el0` plus a link-time-fixed offset. So per-fiber
TLS costs exactly one `msr tpidr_el0, xN` in `rt_switch`, plus copying the
linker's `PT_TLS` image into each fiber's TCB at spawn. aarch64 is Variant I —
TCB at the thread pointer, TLS block above it (strategy.md).

`__tls_get_addr` is needed only for the general- and local-dynamic models, which
static linking relaxes away. An aborting stub is correct for v1 and becomes real
work only if dynamic linking ever does.

Two things follow for free once this exists:

- **`errno` is `_Thread_local int`** — per-fiber exactly as the north star
  requires, with no `__errno_location` indirection to design. The syscall
  wrappers keep returning `-errno` internally (`src/syscall.h:9-11`); the
  translation to the C convention happens only at the libuc boundary, so the
  runtime's own purity is untouched.
- **The stack canary is a plain global on aarch64.** `__stack_chk_guard` is a
  symbol, seeded from `AT_RANDOM`; the x86-64 trap of a hardcoded `%fs:0x28`
  slot does not apply here. It *will* apply at the port, which is why the TCB
  layout must reserve those offsets before it ossifies (strategy.md).

TLS is the item on this page with the worst retrofit cost, because it touches
the context switch and `struct rt_fiber` — both already written. It is the one
piece worth deciding ahead of need.

## The C11 surface

| API | becomes | notes |
|---|---|---|
| `thrd_create` | `rt_fiber_create` + enqueue | on the calling scheduler, always |
| `thrd_join` | park until target is `RT_DEAD`, then drain | "joined" means *drained*, per language.md |
| `thrd_detach` | mark for self-reap at exit | |
| `thrd_yield` | `__libuc_fiber_yield()` | public standard wrapper over today's `rt_fiber_yield()`; no C `yield` keyword |
| `thrd_sleep` | `IORING_OP_TIMEOUT` | `io_uring.h:267` |
| `thrd_exit` | task exit + `tss` destructor pass | |
| `mtx_*` | intra-core wait queue | no atomics; `mtx_timedlock` adds a linked `TIMEOUT` |
| `cnd_*` | same wait queue | `cnd_broadcast` requeues all, resumes none — the scheduler does that |
| `tss_*` | slot array in `struct rt_fiber` | `TSS_DTOR_ITERATIONS` = 4 loop at exit |
| `call_once` | plain flag | no atomics, same reason as `mtx` |

The threading layer is the *small* part of libuc. `task.c` already is it.

## Where the effort actually is

Two items dominate, and neither is threads.

**`malloc`.** The north star files "no allocator" as phase discipline; libuc is
the phase. A per-scheduler arena with `malloc`/`calloc`/`realloc`/`free`/
`aligned_alloc` on top is the first thing a vendored library demands. Design
input already exists in stacks.md; plan.md still lists it as an open question
and should list it as a scheduled deliverable instead.

**`vfprintf`.** Larger than the entire threading layer, because it is a
mini-interpreter for a format language: flags, width, precision (both possibly
`*`), length modifiers `hh h l ll j z t L`, C23's `%b`/`%B` and `%wN`/`%wfN`
fixed-width forms, and beneath it the whole `FILE` layer — buffering modes,
`fwrite`/`fputc` paths, `setvbuf`, `ungetc`, flush-at-exit.

The cliff inside it is **floating point**. `%a` is exact and easy — hex, no
rounding decisions. Correctly-rounded `%e`/`%f`/`%g` is a different problem:
either arbitrary-precision decimal expansion, or a modern shortest-round-trip
algorithm (Ryu, Schubfach). Half of any serious implementation is this path.

So `vfprintf` is a **dial, not a fixed cost**:

| tier | cost | covers |
|---|---|---|
| integers + `%s %c %p %%`, flags/width/precision | small | most systems C, and nearly all *library* error paths |
| add `%a` | trivial | exact, no big-int |
| add correctly-rounded `%e %f %g` | the cliff | anything numeric-facing |
| positional `%1$s`, `%ls`/`%lc`, `%n` | skip | POSIX-not-C, wide chars, and a security footgun |

Vendored *libraries*, as opposed to whole programs, overwhelmingly use `printf`
in error and debug paths only. Tier 1 satisfies most of them, which makes the
scary item much less scary — and the tier boundary is exactly the right place to
stop until a vendored dependency forces the next one.

**The runtime itself never needs any of it.** RT-007's `src/fmt.c` — a hex and
string formatter, ~40 lines — covers the crash handler and all diagnostic
output, and the crash handler must keep using `raw_write` regardless. `vfprintf`
is a purely guest-facing deliverable, triggered by the first vendored translation
unit that calls `printf`, not by libuc existing.

## `printf` acquires the `suspend` effect

A buffered `printf` that flushes through `IORING_OP_WRITE` (`io_uring.h:279`) is
a suspension point. In language.md's lattice that makes `printf` a `suspend`
function, which means it is unavailable in exactly the places the crash handler
and the pre-ring boot path live — the same reason `raw_write` is a sanctioned
exception rather than an embarrassment.

Two practical consequences: the console default must be **line-buffered**, not
fully buffered, or a crash eats the last output; and any guest callback invoked
from a non-`suspend` context must not print. The C world has no way to express
that constraint, which is a concrete example of what the language buys later.

## Not in v1

Dynamic linking, `libc.so`, `dlopen`. `fork`/`exec`/`system`. Signals beyond the
crash handler. Locales and wide characters past C-locale stubs. `setjmp`/
`longjmp` is the likely first addition past minimal — roughly 20 lines of asm,
and vendored C wants it more often than expected (libpng and libjpeg both do).

## Sequencing

This is libuc-tree work, not RT tickets — it belongs to the successor codebase
and starts only once the probe's questions are answered. IDs unassigned
deliberately. The exception is the first row, which is a probe experiment and
could be an RT ticket today.

| work item | depends | trigger |
|---|---|---|
| `tpidr_el0` in `rt_switch`; a guest `_Thread_local` that survives a switch | RT-004 | **probe experiment** — not to fix a layout, but to learn whether per-fiber TLS works at all |
| Per-core arena + `malloc` family | milestone 2 | first vendored library |
| `crt1.o` / `__libc_start_main` / `exit` | RT-006 | first guest `main` |
| `<threads.h>` over `task.c` | above | the conformance claim |
| `FILE` + `vfprintf` tier 1 | arena, ring write | first vendored `printf` |
| `crti.o`/`crtn.o`, empty archives, `ucc` wrapper | crt1 | first unmodified `clang --sysroot` link |
| Vendor one static library end to end | all above | the actual proof |

That last row is the one that matters: the north star's first deliverable class
is vendored static libraries, and nothing in `.scratch/tickets/` currently ends
in "a foreign translation unit linked against us and ran."

## Open questions

- Does the fd table live per-scheduler or per-fiber? Per-scheduler follows shared-nothing,
  but `stdout` is then shared by every fiber on the core and needs a lock that
  invariant 3 says should not exist. A per-scheduler `FILE` with cooperative
  exclusion is probably right, and is not obviously right.
- `thrd_t` identity across a crash-and-respawn supervisor (language.md's
  `restart: OnCrash`) has no C11 answer, because C11 has no supervisors.
- Whether `ucc` should exist at all, or whether shipping a sysroot and letting
  people pass `--sysroot` is the cleaner story.
