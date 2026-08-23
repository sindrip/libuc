# Prior art — has someone already built libuc?

Status: **literature search, 2026-08-18.** No decision here; this is evidence.
strategy.md's reading list is the *positioning* argument — who the neighbours
are and what each concedes. This document is what was actually searched, what
was found, and what is confirmed absent, with the citations that are load-
bearing for specific invariants.

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

What was not found, published or shipped, is anything that **is itself the
libc**. Every close artifact stops at the same boundary, and the boundary is
sharper than "nobody thought of this":

| artifact | mechanism | where it stops |
|---|---|---|
| Capriccio (SOSP '03) | overrides glibc syscall stubs | epoll era; a library over a libc |
| FlexSC-Threads (OSDI '10) | patched-kernel syscall ring | pthread-compatible *alongside* glibc |
| libfibre (SIGMETRICS '20) | epoll/kqueue + M:N, no preemption | layered on glibc; centres load balancing |
| iqiyi/libfiber | hooks ~20 libc symbols, io_uring backend | hooks a libc; inherits its per-thread state |
| Photon (Alibaba) | C++ stackful coroutines over io_uring | C++ library on glibc; no libc ambition |
| Java Loom | JDK blocking calls unmount continuations | dies at the C FFI boundary — see below |
| Zig `std.Io` | parametrise stdlib over blocking/async | needs Zig source; adds a parameter per call |

That column is the novelty claim. It survived both search angles.

## FlexSC is the ancestor — cite it

**Soares & Stumm, "FlexSC: Flexible System Call Scheduling with Exception-Less
System Calls", OSDI 2010.** Follow-up: "Exception-Less System Calls for
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

**Nobody has redone FlexSC on io_uring.** Searched explicitly; the post-2019
FlexSC citation graph goes to *Software-Defined CPU Modes* (HotOS '23) and
*How to Copy Memory?* (SOSP '25), neither of which revisits exception-less
syscalls on a modern ring.

Runner-up ancestor: **Capriccio (von Behren et al., SOSP 2003)**, which
"intercepts blocking I/O calls at the library level by overriding the system
call stub functions". The thesis in one sentence, 23 years old — and the reason
libuc's version is different is entirely the word *overriding*.

## Three findings that are load-bearing for invariants

**WG21 P1364R0 justifies invariant 3.** "Fibers under the magnifying glass"
(2018) argues thread-local access is well-defined for stackless coroutines but
breaks for *stackful* fibers, and that TLS support is a major challenge for
fiber implementations. Its stated escape hatch is that the hazards are avoidable
**if fibers never migrate between OS threads** — i.e. a 1:N model. Shared-nothing
with no migration is not just an aesthetic preference from invariant 3; it is
the published precondition under which per-fiber TLS is sound at all. That
matters for libuc.md's TLS section, whose entire design rests on local-exec
relaxation and a single `msr tpidr_el0`.

*Caveat: the P1364R0 PDF would not text-extract during the search; this is from
search extracts, not a full read. Verify before quoting.* The surrounding
argument is real and multi-round — P0876 (`fiber_context`), P0866R0 (response),
P1520 (response to the response) — which is itself evidence the question is open
rather than settled.

Corroborating, and directly relevant to the Clang build: MSVC ships `/GT`
("support fiber-safe thread-local storage") solely because TLS-address caching
miscompiles under fibers, and **LLVM issue #57260** has the coroutine optimizer
preserving a stale thread id across `co_await` at `-O2`/`-O3`. The toolchain has
to participate, not just the runtime. Worth knowing before the TLS probe
experiment in libuc.md's sequencing table.

**Loom's native-frame pinning is the strongest argument for the be-the-libc
thesis, and it is a negative result.** JDK 24 fixed `synchronized` pinning by
reimplementing monitors against virtual-thread identity. Native frames are
**still unfixed through JDK 25 LTS**: a virtual thread with a JNI or Foreign
Function & Memory frame on its stack cannot unmount, because the JVM cannot
capture and restore the native frame.

This is the precise failure mode of solving fiber suspension from *above* the C
boundary, and it has resisted a decade of very well-funded engineering.
libuc.md's `crt1` inversion — `main` runs on a fiber, so the guest's blocking
calls *are* the suspensions — is the structurally different answer, and nobody
was found attempting it in C. stacks.md:52 already reaches this conclusion from
the stack-layout side; it is the same finding arrived at twice.

**Ringmaster independently derived invariant 1's exception list.**
*Ringmaster: How to juggle high-throughput host OS system calls from TrustZone
TEEs* (arXiv 2601.16448, Jan 2026) maps Normal-world io_uring SQ/CQ memory into
a TrustZone enclave so the enclave can issue host syscalls through the ring. It
is the only published system found that treats io_uring as a **system-call**
conduit rather than an I/O API.

The striking part is its Table 1. The calls it keeps *off* the ring —
`mmap`/`munmap`/`mprotect`, scheduling, signal handling, futex/pthread
primitives — are almost exactly this project's direct-syscall list, reached from
a completely unrelated motivation. Two projects deriving the same cut from
different premises is evidence the boundary is a property of the io_uring ABI
rather than an arbitrary choice.

It differs where it counts: the split is a **trust partition**, not an ABI
claim; the paper never argues io_uring is a general syscall interface; and it
**polls promises** in a `while` loop rather than suspending anything. No
scheduler, no libc, no per-scheduler ring discipline.

## Not novel — do not claim it

**Thread-per-core, shared-nothing, one ring per pinned core.** Standard practice:
Seastar/ScyllaDB, Glommio, monoio, compio, Redpanda, Apache Iggy. The substrate
is conventional and there is no academic evaluation of it either — it lives
entirely in industrial blog posts.

The novelty is not the substrate. It is **refusing to colour the API on top of
it**: every one of those keeps `async fn` or futures/continuations. That is the
sentence to use when describing this project to someone who knows Seastar.

## Citable disagreements

Two published positions contradict specific invariants. Both are worth naming
rather than ignoring — a design is stronger for knowing who disagrees.

- **Demikernel (SOSP '21)** deliberately *replaced* POSIX with a queue push/pop
  interface (PDPIX) because POSIX was judged unsuitable at microsecond scale.
  libuc's whole bet is that the POSIX shape is retainable if the blocking is a
  fiber suspension. This is a direct, citable disagreement about the central
  premise.
- **libfibre (Karsten & Barghi, POMACS 4(1) / SIGMETRICS '20, DOI
  10.1145/3379483)** establishes the blocking-API-costs-nothing premise
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

## Confirmed absent — open ground

Each of these returned nothing across both search angles:

- **A libc whose blocking calls are fiber suspensions over io_uring.** The core
  claim. Nothing published; no serious unpublished project with a design
  document.
- **Per-fiber allocator.** Zero hits — not even a problem statement. What breaks
  when a vendored library's `malloc` assumes per-thread arenas under a fiber
  scheduler is unwritten. Directly relevant to libuc.md's `mem/` deliverable.
- **Per-fiber `errno` in a C library.** Exists as standards-committee argument
  and compiler bugs, never as a paper. libuc has sidestepped the hard half by
  having no `errno` at all internally (`src/syscall.h`), translating only at the
  boundary.
- **The 7.2 BPF `struct_ops` in-kernel event loop** (`io_uring/loop.c`,
  `io_uring/bpf-ops.c`). LWN and LKML only — LWN Articles/1062286, /1046950,
  /1024361, /847951, plus Begunkov's RFC series. **Zero peer-reviewed work.**
  This corroborates strategy.md's wedge #3: first published numbers here would
  be a flag planted on genuinely empty ground.
- **`SINGLE_ISSUER`/`DEFER_TASKRUN` vs `SQPOLL` exclusivity** as an evaluated
  architecture. Nothing — despite it being invariant 2 and verified in-tree at
  `out/src/io_uring/io_uring.c:2815-2821`.
- **Linking vendored *static* C libraries against a fiber-scheduling libc.** The
  nearest analogue is Loom's native-frame pinning, which is a problem report,
  not a solution. libuc.md names this as its central risk; the literature
  confirms nobody has retired it.
- **A freestanding, no-libc runtime as PID 1 with io_uring as its syscall ABI.**
  Nothing in any venue. The unikernel literature (Unikraft, OSv, HermiTux,
  Lupine) occupies adjacent ground but always with a libc — usually musl —
  inside the image and *synchronous* syscall shims.
- **An academic design paper on musl or glibc.** There is no "musl paper" to
  cite; libc design is documented only in source and mailing lists.

## Worth reading before building more

- **iqiyi/libfiber** — the closest live artifact. Hooks `read`/`write`/`recv`/
  `send`/`accept`/`connect`/`poll`/`epoll_*`/`getaddrinfo` and friends, with an
  io_uring backend, in production at iQiyi. libuc's user-visible semantics
  already ship. Read it for what the hook boundary cannot cover, which is the
  argument for owning the libc.
- **Bojie Li, arXiv 2607.02630** (Jul 2026) — an `LD_PRELOAD` fiber runtime,
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

Three coverage holes, recorded so a later search does not repeat them:

- **dblp's tokeniser rejects the underscore** — `q=io_uring` returns 0 while
  demonstrably indexing the papers. Do not trust dblp negatives here.
- **Semantic Scholar returned HTTP 429 twice.** That axis is uncovered.
- **No PDF text extraction available** (`pdftotext`, `qpdf`, `mutool`, `pypdf`
  all absent). **P1364R0 remains unverified** on that account and is flagged
  inline above. The libfibre claims were since confirmed directly from the
  author's paper page and the repository source, and no longer rest on
  extracts.

One item could not be verified at all: *"Dreaming of Syscall-less I/O with
io_uring"* (dl.gi.de, apparently BTW 2025 / LNI). The PDF endpoint returned a
login page. Snippets suggest a tutorial rather than an architectural proposal,
but it is unconfirmed.

**Recency.** The literature is not merely young, it is largely absent, and what
exists is written against 5.x kernels. `IORING_OP_BIND`/`LISTEN` are untouched,
so "socket setup through the ring" has no coverage at all; likewise
`IORING_SETUP_SQ_REWIND`, `query.c` capability probing, and the BPF loop. The
newest kernel any paper engages with is 6.17 (uringscope). **Nothing in the
literature is written against 7.2.**
