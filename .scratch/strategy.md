# Strategy notes

Status: **reviewed 2026-08-30.** Positioning and long-range constraints only;
`AGENTS.md` is the authority for current invariants and `.scratch/plan.md` for
current sequencing.

## Platform decree

The target is **64-bit little-endian Linux**. aarch64 and x86-64 both build
today. Adding another architecture should add an architecture directory and a
cross file without changing generic source.

The VM pins Linux 7.2 so development has one inspectable kernel. A hosted libuc
would instead establish a kernel-version floor and probe the facilities it
needs at initialization. That is a capability contract, not a compatibility
matrix.

## Per-architecture seam

The seam is intentionally small:

- entry, context switching, thread-pointer access, and direct-syscall trap
  conventions;
- TLS ABI details (Variant I on aarch64, Variant II on x86-64);
- instruction-set-specific string implementations; and
- any future interaction with architecture control-flow protection.

x86-64 requires FSGSBASE for the current thread-pointer implementation. There
is no `arch_prctl`-per-switch fallback: `arch_prctl` is not in the direct-
syscall allowlist, and a syscall on every switch would be the wrong mechanism
anyway. Initialization must reject a machine that lacks the required facility.

Any future stack-protected guest ABI must first verify and then reserve the TCB
offsets its compiler expects. The current freestanding runtime disables stack
protectors and must not acquire speculative libc TCB layout as if it were an
ABI fact.

aarch64's weaker memory model remains useful: it exposes missing
acquire/release operations that x86 TSO can hide. Both architectures are now
first-class build targets, not a primary port and a future validation port.

## Self-hosting, if the language earns it

Self-hosting can be gradual because generated objects should use the target
platform's C ABI. Individual leaf functions could move first, followed by
larger runtime components; architecture-specific entry, switching, and trap
code remain architecture-specific regardless of their source notation.

A compiler bootstrap would need a deterministic three-stage fixpoint and a
maintained seed implementation. `language.md` deliberately leaves the exact
restricted-runtime dialect open; do not make current C architecture depend on
that language design.

## What to measure

The runtime should publish measurements only after the relevant path exists.
Useful comparisons include:

- throughput and tail latency under saturation;
- memory per live connection or fiber;
- cancellation completion and shutdown latency;
- warm-path stack/TLS allocation cost; and
- the cost of calling vendored C through libuc compared with language runtimes
  that require an FFI transition.

TechEmpower, language benchmark suites, and BPF-loop measurements may later be
useful distribution artifacts, but they are not architectural evidence today.
Avoid predicted rankings or multipliers before a reproducible harness exists.

## Adoption sequence

1. Demonstrate the C runtime and publish its harness.
2. Offer libuc as a linkable C library whose supported blocking calls suspend
   the current fiber over io_uring.
3. Measure the in-kernel BPF loop if it survives the validation in
   `bpf-loop.md`.
4. Add a language only after the runtime has exposed what the language needs.

The durable pitch is narrower than "a fast new language": io_uring is the
syscall ABI, blocking is suspension, cancellation owns the underlying
operation, and C calls do not cross a foreign-runtime trampoline.

## Prior art

`prior-art.md` records the dated literature search and its limitations. The
closest systems each cover only part of the combination: libc interception,
user-level fibers, async standard-library interfaces, or ring-based syscall
delivery. Treat the combination as a working differentiation, not a permanent
novelty claim; rerun the search before publishing it.
