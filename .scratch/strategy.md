# Strategy notes

Status: **conversation-derived, 2026-08-18.** Positioning, portability, and
self-hosting — nothing here changes a current ticket. AGENTS.md's North star
section is the authority this file elaborates.

## Platform decree

**64-bit little-endian Linux.** aarch64 is the first architecture, not the
only one; the decree is what the language and ABI are allowed to assume
(pointer = `u64`, LE serialization), so adding x86-64/riscv64 never
reintroduces portability types. Excludes 32-bit; a thread-per-core io_uring
runtime was never going to serve armv7 anyway.

When the runtime later runs hosted: kernel version becomes a *floor* plus
`query.c`-style capability probing at init — never a compat matrix.

## The per-arch seam (what actually varies)

Small, musl-`arch/`-shaped:

- context switch asm (~50 lines/arch) and the thread-pointer mechanism:
  `msr tpidr_el0` is free on aarch64; x86-64 needs `wrfsbase` gated on
  FSGSBASE (fallback `arch_prctl` is a syscall per switch — gate on the CPU
  bit).
- TLS variant: aarch64 is Variant I (blocks above TP), x86-64 Variant II
  (below). Keep the TLS-layout interface arch-neutral from day one.
- **x86-64 hardcodes the stack canary at `%fs:0x28` and the TCB self-pointer
  at `%fs:0x0`.** Any future TCB layout must reserve those slots at those
  offsets or every stack-protected guest function crashes. aarch64 spared us
  this (global guard); do not let the aarch64-shaped TCB ossify.
- syscall numbers: write wrappers against the modern asm-generic subset
  (`openat`, `clone`, `ppoll`, `clock_gettime` — already the rule here);
  each arch supplies numbers and the trap instruction only.
- PAC/BTI ↔ CET/shadow-stack are per-arch analogs; keep "what does a context
  switch owe the CFI machinery" an abstract question, not an aarch64 answer.

Developing on aarch64 first is the correct *direction*: its weak memory model
surfaces missing acquire/release that x86-TSO silently forgives. The x86-64
port should be a validation event, not a debugging event.

## Self-hosting (far future, but the shape matters now)

- **Per-symbol migration.** Because the future language emits AAPCS64 objects,
  a function written in it is a drop-in `.o` in the runtime's archive. Self-
  hosting is a gradient walked one symbol at a time — unlike Go's big-bang 1.5
  conversion, which its C-incompatible ABI forced. Leaves first (`mem*`,
  string), allocator, then scheduler/ring; `_start`, the switch, and syscall
  stubs stay asm forever (~200 lines).
- Verify each migrated symbol with differential testing against musl and
  musl's libc-test suite.
- Compiler bootstrap: classic three-stage fixpoint (stage2 ≡ stage3
  bit-identical). Requires a deterministic compiler — no hash-map iteration
  order in output, no timestamps — enforced from the first commit of the
  compiler, not retrofitted. Keep the seed compiler maintained as the
  from-nothing path.
- The gating language feature is the checked `bare` dialect (language.md):
  code provably free of alloc/suspend/runtime calls, i.e. what `src/` already
  is in C.

## Benchmarks — what is winnable and what is not

- **M3-class numbers are the proof artifact, pre-language.** The echo/HTTP
  results come from the C substrate; do not gate them on a compiler existing.
- **TechEmpower: reach the top cluster, don't chase first place.** The top of
  plaintext/JSON is already thread-per-core designs at NIC/kernel saturation,
  within a few percent of each other. The available headline is a *new
  language* sitting in the C/Rust cluster, 2–5× above Go/Node/Java — no young
  language has that. Prerequisites: the x86-64 port (TFB runs x86) and bare
  metal.
- **Benchmarks Game: no.** CPU shootouts are codegen contests; emitting C caps
  the downside but "top" is off the table and tests nothing this design
  claims.
- **The winnable, definable territory: tail latency and density.** p99.9
  flatness under saturation (no global GC, no work stealing, cancellation that
  cancels) and connections-per-gigabyte (multishot means density doesn't even
  require task-per-connection). No famous leaderboard exists — TigerBeetle's
  playbook: define the benchmark the architecture is honest at, publish the
  harness.
- **FFI microbenchmarks**: call-into-C cost vs cgo/JNI/NIF is the headline
  table for the libuc wedge.

## Adoption wedges, cheapest first

1. M3 numbers + the harness (credibility).
2. **libuc as link-in**: "link your C program against libuc; blocking calls
   become task suspensions over the ring" — try-it-in-an-afternoon, no
   language adoption required. Foreactor as a platform instead of a shim.
3. First published numbers for the in-kernel BPF loop (bpf.md #8) — weeks of
   work for a flag planted on the most futuristic ground.
4. The language (long play).

## Prior art / reading list

Positioning only. **prior-art.md is the evidence** — what was searched, what is
confirmed absent, and which citations are load-bearing for which invariant.

Nearest neighbors, each missing exactly one axis:

- **Junction (NSDI '24)** — userspace libOS running unmodified Linux binaries
  over kernel bypass; libuc's shape, wrong I/O substrate, compat-first.
- **High-Performance DBMSs with io_uring (arXiv 2512.04859)** — measures
  exactly this architecture (ring per pinned thread, fibers on CQEs) without
  building the platform.
- **Foreactor (arXiv 2409.01580)** — POSIX interception onto io_uring;
  shim, not platform. **uringscope (arXiv 2606.15137)** — observability.
- **Lupine Linux (EuroSys '20), UKL** — the PID-1/tinyconfig posture as
  product; here it is scaffolding.
- **Demikernel (SOSP '21), Shenango/Caladan** — datapath-OS and user-level
  scheduling lineage.
- **Zig `std.Io`** — the industry project converging on this model from the
  language side; the competitive clock. Also: Photon (C++ fibers+uring),
  Loom's pinning rules (what FFI-vs-density compromise looks like), Zpoline
  (ATC '23 syscall rewriting).

The structural read: everyone arrives from an installed base (languages from
the top, kernel from the bottom, academia compat-sideways, products from
inside) and pays for it with compromises. Starting from the target is only
available to the unencumbered; the risk is worse-is-better — a good-enough
compromise (Loom, Zig) capturing the demand first. The pitch therefore leans
on what retrofits structurally cannot reach: trampoline-free FFI, cancellation
that cancels the I/O, a libc where blocking *is* suspension, invariants
installed as kernel policy (bpf.md).
