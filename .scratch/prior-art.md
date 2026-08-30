# Prior art — has someone already built libuc?

Status: **literature search performed 2026-08-18; reviewed and amended
2026-08-30** (folded in: Silk, green-man, Junction, the .NET green-thread
negative, helio). No decision here; this is dated evidence, not a continuously
verified survey.
strategy.md's reading list is the *positioning* argument — who the neighbours
are and what each concedes. This document is what was actually searched, what
was found, and what that dated search did not find, with the citations that are
load-bearing for specific invariants.

The question asked: **has anyone published, or shipped, a libc whose blocking
calls are fiber suspensions over io_uring?**

## Verdict

Novel in the combination. Not novel in any single ingredient.

Every component has prior art, some of it twenty years old: transparent
blocking-to-async conversion at the libc stub layer (Capriccio, 2003), a
POSIX-compatible thread package that turns legacy blocking syscalls into
submissions on a shared-memory ring (FlexSC-Threads, 2010), syscall
interception generated *at libc level* (Unikraft, 2021), and function-colour
elimination via effect handlers (OCaml 5, 2021).

What was not found, published or shipped, is a libc whose ordinary blocking
calls **suspend stackful fibers over io_uring**. Ringmaster is a real
libc-over-io_uring counterexample to the broader claim: its modified musl
enqueues I/O and then synchronously polls a promise. The remaining boundary is
the scheduler and suspension semantics:

| artifact | mechanism | where it stops |
|---|---|---|
| Capriccio (SOSP '03) | overrides glibc syscall stubs | epoll era; a library over a libc |
| FlexSC-Threads (OSDI '10) | patched-kernel syscall ring | pthread-compatible *alongside* glibc |
| libfibre (SIGMETRICS '20) | epoll/kqueue + M:N, no preemption | layered on glibc; centres load balancing |
| iqiyi/libfiber | hooks ~20 libc symbols, io_uring backend | hooks a libc; inherits its per-thread state |
| Photon (Alibaba) | C++ stackful coroutines over io_uring | C++ library on glibc; no libc ambition |
| Java Loom | JDK blocking calls unmount continuations | dies at the C FFI boundary — see below |
| Zig `std.Io` | parametrise stdlib over blocking/async | needs Zig source; adds a parameter per call |
| Junction (NSDI '24) | loader swaps in a modified glibc; syscalls suspend user threads | kernel-bypass dataplane; swaps a glibc rather than being one |
| Ringmaster ('26) | modified musl dispatches I/O through io_uring | busy-polls promises; TrustZone split, no fiber scheduler |
| Silk (ClickHouse '26) | C++ stackful fibers over per-CPU rings | library on glibc; own verbs; steals across CPUs |
| green-man ('26) | C green threads over io_uring, one scheduler thread | hosted PoC; its own async wrappers, not the POSIX names |

That column is the working novelty claim from this search. It survived both
search angles, but must be rechecked before publication.

## FlexSC is the ancestor — cite it

**Soares & Stumm, ["FlexSC: Flexible System Call Scheduling with Exception-Less
System Calls"](https://www.usenix.org/conference/osdi10/flexsc-flexible-system-call-scheduling-exception-less-system-calls),
OSDI 2010.** Follow-up: "Exception-Less System Calls for
Event-Driven Servers", USENIX ATC 2011 (`libflexsc`).

Syscalls become entries in a shared-memory request/response ring polled by
kernel-side syscall threads. It ships **FlexSC-Threads**: "a user-mode thread
package, binary compatible with POSIX threads, that translates legacy
synchronous system calls into exception-less ones transparently to
applications." Apache +116%, MySQL +40%, BIND +105%, all unmodified.

That is libuc's mechanism, stated nine years before io_uring existed, with the
same motivation this project uses — the trap, the pipeline flush, the cache and
TLB pollution. It differs in four ways that matter:

- It required a **patched kernel**; the ring was theirs, not the OS's. That is
  why it died as an artifact, and it is exactly the thing io_uring fixes.
- Generic kernel syscall-thread pool, not opcode-specific completion.
- **M:N with migration and load balancing** — invariant 3 forbids this.
- It ran *under* glibc. Per-fiber `errno`/TLS was a non-issue precisely because
  pthread binary compatibility meant inheriting glibc's per-OS-thread TLS.

**No FlexSC-on-io_uring artifact was found.** The search was explicit; the
post-2019 FlexSC citation graph goes to *Software-Defined CPU Modes* (HotOS '23)
and
*How to Copy Memory?* (SOSP '25), neither of which revisits exception-less
syscalls on a modern ring.

Runner-up ancestor: **Capriccio (von Behren et al., SOSP 2003)**, which
"intercepts blocking I/O calls at the library level by overriding the system
call stub functions". The thesis in one sentence, 23 years old — and the reason
libuc's version is different is entirely the word *overriding*.

## Junction is the nearest modern miss

**Fried et al., "Making Kernel Bypass Practical for the Cloud with Junction",
NSDI 2024** (`github.com/JunctionOS/junction`). A libOS in the
Shenango/Caladan lineage that runs unmodified Linux binaries: its ELF loader
transparently swaps in a modified glibc, so nearly every syscall calls into
Junction's user-level scheduler instead of trapping — blocking calls suspend
user threads, and whole managed runtimes (Go, Java, Node, Python) run on top
unmodified. It agrees the **libc is the right interception boundary** and puts
a thread scheduler under it.

It stops short of the core claim on three counts: the dataplane is kernel
bypass on dedicated cores, not io_uring on a stock kernel; it swaps a hosted
glibc at load time rather than being the platform libc; and binary
compatibility with the whole POSIX surface is its headline constraint — the
exact scope the vendored-static-libraries-first plan defers. Missed by the
2026-08-18 search; found in the 2026-08-30 amendment.

## Three findings that are load-bearing for invariants

**[WG21 P1364R0](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p1364r0.pdf)
supports invariant 3's no-migration pressure.** "Fibers under the magnifying
glass" (2018) documents the TLS-address caching hazard in an N:M design: a
stackful fiber can resume on a different OS thread after compiler-generated
code has retained an address into the first thread's TLS. It identifies 1:N as
avoiding that migration hazard, while also noting that an unadapted blocking
call then blocks the sole OS thread. This supports no migration; it is not a
blanket proof that every per-fiber TLS implementation is sound. libuc's own
argument still depends on restoring each fiber's thread pointer and preserving
compiler assumptions across the switch.

The surrounding argument is multi-round — P0876 (`fiber_context`), P0866R0
(response), P1520 (response to the response) — which is itself evidence the
question is open rather than settled.

Corroborating, and directly relevant to the Clang build: MSVC ships `/GT`
("support fiber-safe thread-local storage") solely because TLS-address caching
miscompiles under fibers, and **LLVM issue #57260** has the coroutine optimizer
preserving a stale thread id across `co_await` at `-O2`/`-O3`. The toolchain has
to participate, not just the runtime. libuc now installs a fiber's TLS block on
every switch; any optimization of that path must preserve the same semantics.

**Loom's native-frame pinning is the strongest argument for the be-the-libc
thesis, and it is a negative result.** [JDK 24 removed monitor
pinning](https://openjdk.org/jeps/491), but
[OpenJDK's virtual-thread design](https://openjdk.org/jeps/444) retains pinning
while a thread runs native code or a foreign function because that frame cannot
be unmounted. Later work is narrower rather than a blanket reversal: a
[JDK 26 change](https://bugs.openjdk.org/browse/JDK-8369238) permits preemption
at some VM-internal class-initialization waits.

This is the precise failure mode of solving fiber suspension from *above* the C
boundary. [.NET's green-thread
experiment](https://github.com/dotnet/runtimelab/issues/2398) provides adjacent
evidence, not the same conclusion: it was put on hold primarily to avoid
introducing another programming model, while its report also measured a minimal
10⁸-call P/Invoke benchmark rising from 300ms to about 1800ms and recorded
thread-local/native-state and shadow-stack problems.
libuc.md's startup inversion — guest code runs on a fiber, so its blocking
calls *are* the suspensions — is the structurally different answer, and this
search found no C libc implementation combining the pieces. `stacks.md`
reaches the same conclusion from the stack-layout side.

**[Ringmaster](https://arxiv.org/abs/2601.16448) is the closest libc-over-ring
counterexample.** *Ringmaster: How to juggle high-throughput host OS system
calls from TrustZone TEEs* (Jan 2026) maps Normal-world io_uring SQ/CQ memory
into a TrustZone enclave and provides a musl-based Ringmaster LibC whose I/O
calls enqueue SQEs.

Its Table 1 draws a related boundary: memory-management, scheduling, signals,
and pthread primitives stay in the trusted Ringmaster OS, while I/O goes to
Linux through the ring. The resemblance is evidence that io_uring naturally
partitions a libc surface, but it is not invariant 1's exception list: the
paper's boundary is a trust partition, and it keeps futex/pthread operations
off-ring where pinned Linux 7.2 supplies a futex opcode.

The decisive difference is waiting. Ringmaster's synchronous API busy-polls a
promise until the CQE arrives; it has no fiber scheduler, suspension, or
per-scheduler ring discipline. It disproves "nobody built a libc over
io_uring," but not libuc's narrower blocking-call-as-fiber-suspension claim.

## Not novel — do not claim it

**Pinned executors, shared-nothing, and private rings.** Standard practice:
Seastar/ScyllaDB, Glommio, monoio, compio, Redpanda, Apache Iggy,
helio/DragonflyDB (whose `fb2` fibers are reworked Boost.Fiber over an
io_uring loop), plus a long tail of C++20-coroutine io_uring wrappers
(co_context, liburing4cpp, zedio, …). The substrate is conventional and there
is no academic evaluation of it either — it lives entirely in industrial blog
posts.

libuc's unit is the scheduler task/thread, not the CPU: a placement policy may
put one or more schedulers on a CPU, while each ring remains owned by its single
issuer.

The novelty is not the substrate — and, after Photon, helio and Silk, not
uncoloured stackful blocking over io_uring either: that trio already lets
plain functions block on fibers. What none of them touches is **whose names do
the blocking**: each ships its own verb set as a library over glibc, where
libuc's claim is that the uncoloured blocking surface is the POSIX surface
itself, because the runtime is the libc. That is the sentence to use when
describing this project to someone who knows Seastar.

## Citable disagreements

Two published positions contradict specific invariants. Both are worth naming
rather than ignoring — a design is stronger for knowing who disagrees.

- **Demikernel (SOSP '21)** deliberately *replaced* POSIX with a queue push/pop
  interface (PDPIX) because POSIX was judged unsuitable at microsecond scale.
  libuc's whole bet is that the POSIX shape is retainable if the blocking is a
  fiber suspension. This is a direct, citable disagreement about the central
  premise.
- **[libfibre](https://cs.uwaterloo.ca/~mkarsten/papers/sigmetrics2020.html)
  (Karsten & Barghi, POMACS 4(1) / SIGMETRICS '20, DOI 10.1145/3379483)**
  establishes the blocking-API-costs-nothing premise
  rigorously, with a real artifact — but its headline positive result is *load
  balancing across cores*, which invariant 3 forbids outright. It is the closest
  published counter-position to shared-nothing.

  Details, since it is the most-cited neighbour: a C++ M:N runtime, GPLv3,
  repo created 2017-10-24, descended from Barghi's earlier `uThreads`. The
  abstract's two named components are load balancing and *user-level I/O
  blocking*. It agrees with invariant 7 — the README calls it "M:N user-level
  threading runtime **without preemption**" — and disagrees with invariant 3.
  **The I/O backend is epoll/kqueue, verified in `src/libfibre/Poller.h`**
  (`<sys/epoll.h>`/`<sys/eventfd.h>`/`<sys/timerfd.h>` on Linux, `<sys/event.h>`
  on FreeBSD); a vestigial `OLDURING` flag survives in `Makefile.config` but the
  shipped poller is readiness-based. The 2017 codebase predates io_uring being
  usable (5.1, May 2019), which is the whole reason the axis is open.
  **GPLv3: read for ideas, cannot be vendored.**

- **Silk (ClickHouse, 2026 — `github.com/ClickHouse/silk`,
  `clickhouse.com/blog/silk`)** is that counter-position rebuilt on io_uring,
  and current: C++ stackful fibers, one pinned scheduler thread per CPU each
  owning its own ring, and **topology-aware work stealing** (hyperthread
  sibling, then socket, then cross-socket) as the headline feature —
  invariant 3's exact opposite, argued on this project's own substrate with
  reproducible benchmarks (~3.6ns yield, ~7.6µs ring ping-pong, 5.9M file
  IOPS). It agrees on much else: cooperative yields, io_uring "as the I/O
  ground truth rather than a backend", zero steady-state heap allocation,
  synchronization primitives carrying their queue nodes inline in the fiber.
  One finding transfers directly: it rebuts Photon's measured 13%
  stackful-fiber penalty by attributing the overhead to slab-allocated stacks
  aliasing in the cache — page-aligned `mmap`'d stacks with guard pages avoid
  it — which bears on the per-scheduler stack pool. Repo created 2026-05-05,
  Apache-2.0: readable *and* vendorable, unlike libfibre.

## Not found in the dated search — open ground

Each of these returned nothing across both search angles:

- **A libc whose blocking calls are fiber suspensions over io_uring.** The core
  claim. Ringmaster reaches libc + ring but busy-polls; Junction reaches libc +
  suspension but uses a kernel-bypass dataplane. Neither combines all three.
- **Per-fiber allocator.** This search found no direct treatment, not even a
  problem statement. What breaks
  when a vendored library's `malloc` assumes per-thread arenas under a fiber
  scheduler is unwritten. Directly relevant to `libuc.md`'s allocator work.
- **Per-fiber `errno` implemented by a C library.** Bojie names preservation of
  per-fiber libc state as a requirement in an `LD_PRELOAD` runtime, but this
  search found no libc implementation. libuc has no `errno` internally
  (`src/syscall.h`) and translates only at the public boundary.
- **The 7.2 BPF `struct_ops` in-kernel event loop** (`io_uring/loop.c`,
  `io_uring/bpf-ops.c`). LWN and LKML only — LWN Articles/1062286, /1046950,
  /1024361, /847951, plus Begunkov's RFC series. **Zero peer-reviewed work.**
  This corroborates `strategy.md`'s adoption item 3: first published numbers
  here would
  be a flag planted on genuinely empty ground.
- **`SINGLE_ISSUER`/`DEFER_TASKRUN` vs `SQPOLL` exclusivity** as an evaluated
  architecture. Nothing — despite it being invariant 2 and verified in-tree at
  `out/src/io_uring/io_uring.c:2815-2821`.
- **Linking vendored *static* C libraries against a fiber-scheduling libc.** The
  nearest analogue is Loom's native-frame pinning, which is a problem report,
  not a solution. `libuc.md` names this as its central risk; this search found
  no artifact that had retired it.
- **A freestanding, no-libc runtime as PID 1 with io_uring as its syscall ABI.**
  Nothing in any venue. The unikernel literature (Unikraft, OSv, HermiTux,
  Lupine) occupies adjacent ground but always with a libc — usually musl —
  inside the image and *synchronous* syscall shims.
- **An academic design paper on musl or glibc.** There is no "musl paper" to
  cite; libc design is documented only in source and mailing lists.

## Worth reading before building more

- **iqiyi/libfiber** — the closest live artifact. Hooks `read`/`write`/`recv`/
  `send`/`accept`/`connect`/`poll`/`epoll_*`/`getaddrinfo` and friends, with an
  io_uring backend, in production at iQiyi. It demonstrates that similar
  user-visible blocking-over-fiber semantics can ship. Read it for what the
  hook boundary cannot cover, which is the
  argument for owning the libc.
- **[Bojie Li, arXiv 2607.02630](https://arxiv.org/abs/2607.02630)** (Jul 2026)
  — an `LD_PRELOAD` fiber runtime,
  17.3x on an unmodified thread-per-connection binary. Two things earn it a
  read: it is the only paper found that names the **per-fiber libc state**
  problem out loud ("saving and restoring per-fiber libc state such as `errno`
  across switches"), and it distinguishes servers that "block at the libc layer"
  (fiberizable) from engines like InnoDB that "synchronize below libc" (not).
  That distinction is the precise statement of why hooking is fragile and being
  the libc is not.
- **Pestka, Paradies, Pohl, arXiv 2411.16254** — a position paper that names
  "libc and libc++ do not support io_uring" as *the* adoption barrier. Useful as
  a citation that this project's problem is an acknowledged open one.

## Method, and how far to trust the negatives

Two independent searches, one from the io_uring/syscall-ABI side and one from
the libc/user-level-threading side. Both converged on FlexSC as the ancestor and
on the same core negative without prompting, which is the main reason to believe
the result.

**The strong caveat: this subfield is not on arxiv.** `abs:"io_uring"` returns
**seven papers in total**, all storage/DBMS/observability/TEE-shaped. A bare
`all:uring` sweep confirmed the ceiling is real rather than a tokenisation
artifact. The work lives at USENIX, SYSTOR, CHEOPS, PLOS, VLDB, and in
engineering blogs. **Treat arxiv negatives as weak evidence**; the strong
negatives above come from converging web searches, not an exhaustive index.

Coverage notes, recorded so a later search does not repeat the same work:

- **dblp's tokeniser rejects the underscore** — `q=io_uring` returns 0 while
  demonstrably indexing the papers. Do not trust dblp negatives here.
- **Semantic Scholar returned HTTP 429 twice.** That axis is uncovered.
- The initial local toolset had no PDF text extractor. The 2026-08-30 review
  read P1364R0 and Ringmaster from their primary publications; their claims no
  longer rest on search snippets. The libfibre claims were likewise confirmed
  from the author's paper page and repository source.

One item could not be verified at all: *"Dreaming of Syscall-less I/O with
io_uring"* (dl.gi.de, apparently BTW 2025 / LNI). The PDF endpoint returned a
login page. Snippets suggest a tutorial rather than an architectural proposal,
but it is unconfirmed.

**Recency.** The reviewed work does not cover Linux 7.2-specific behavior.
`IORING_OP_BIND`/`LISTEN`, `IORING_SETUP_SQ_REWIND`, `query.c` capability
probing, and the BPF loop are untouched in these papers. That is a bounded
claim about this dated corpus, not a claim that no later artifact exists.
