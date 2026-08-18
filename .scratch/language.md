# The language — a draft answer to the deferred question

Status: **conversation-derived draft, 2026-08-18.** plan.md defers the language
deliberately: "answered later by what the runtime actually turns out to need."
This file is a first draft of that answer, written top-down; the runtime keeps
authority. Nothing here blocks or changes any current ticket. One standing
observation does apply now: **the C runtime is the `bare` layer's requirements
document** — everything `src/` does is something the language must eventually
express under its most restricted dialect.

## The load-bearing idea: the effect lattice

Every hard runtime trade-off met so far resolves the same way: make the
capability a *checked effect*, infer it from the call graph, let the compiler
pick the substrate. The effects:

| effect | grants | its absence enables |
|---|---|---|
| `suspend` | ring I/O, yielding, channel ops | code that runs where the scheduler can't (signal/crash paths) |
| `alloc` | arena allocation | code that runs before/inside the allocator |
| `calls-C` | `extern "C"` calls | **dense slab stacks** — see stacks.md; no C reachable → millions-scale density at zero FFI cost |
| `bpf` (restriction set) | — | compiling to BPF bytecode: kernel offload as a dialect, auto-attached filters/hooks — see bpf.md #2 |

`bare` = ¬`suspend` ∧ ¬`alloc`. It is **orthogonal to `calls-C`**: bare code
calls C freely — it must, since per-symbol self-hosting means bare functions
link against the remaining C runtime for years (strategy.md).

Two consequences that make the lattice honest:

- **Effects live in function *types*.** Reachability through function pointers
  is undecidable otherwise — an indirect call would conservatively poison
  every effect. `suspend` forces this anyway (a callback that might park must
  say so), so it is one mechanism, not a per-effect tax; but it does mean
  effect signatures are part of the API surface, and inference only fills in
  what annotations bound.
- The FFI-callback rule falls out: a callback passed to C from a dense-stack
  task must be typed non-`suspend`, or the shared per-core C stack is held by
  a parked task (stacks.md, "borrowed-C-stack hazard").

Contrast: Go's `//go:nosplit` pragmas are unchecked; Go/Zig/Loom treat stack
strategy as one global decision and pay the worst case everywhere (cgo's
trampoline, Loom's pinning). Effect-directed per-task substrate selection has
no prior art found. This table, not the syntax, is the language.

## Errors, cancellation, and `?`

`option` and `result` are ordinary library sum types. The only compiler magic
is `match` and `?`, and `?` desugars through a `propagate` trait — which the
runtime's `Cancelled` also implements. A deadline expiring issues
`IORING_OP_ASYNC_CANCEL` on the pending SQE; the suspension point returns
`Cancelled`; `?` lifts it like any other error. **Cancellation is a value in
the same channel as errors, and it requests cancellation of the actual I/O** —
no context plumbing, no cooperative flags.

*Requests*, because the soundness rule underneath is non-negotiable:
`ASYNC_CANCEL` is a request, and the original operation still completes — with
its result or `-ECANCELED` — and may write into its buffer until that CQE is
reaped. So a suspension point resumes only when the operation's own CQE
arrives, and **task teardown (cancel, crash, scope exit) means drain in-flight
CQEs, then drop memory** — never unwind-and-free. The full rule and the
per-core-pool buffer design that makes it cheap: transport.md, "Buffer
lifetime". This is the mistake that made early Rust io_uring runtimes unsound;
it is a semantics decision, not an implementation detail.

## Types

- `type` is the only declaration keyword. Sums on the RHS
  (`type result<T, E> = Ok(T) | Err(E)`), inline record payloads
  (`TooLarge { size: u64 }`), records in braces. No `enum` keyword.
- **No type aliases.** A bare type expression after `=` is a parse error.
  Types are either spelled structurally, literally, every time, or nominal —
  no third "named but secretly the same" category. Newtype is a
  one-constructor sum, zero-cost by representation rule.
- **No `as` casts.** Lossless integer widening is implicit; everything else is
  a named operation: `truncate<T>()`, `checked<T>()` → `option` (composes with
  `?`), `saturating`, `wrapping`, `bitcast<T>()`. Pointer conversions are
  `unsafe`-only, with ptr↔int (`p.addr`, `from_addr<T>()`) deliberately
  distinct from ptr↔ptr (`p.cast<T>()`) for provenance.
- **No `usize`.** Platform decree: 64-bit little-endian Linux only (see
  strategy.md). Pointer-sized is `u64`; the most common cast in systems code
  is deleted rather than renamed.
- No user-defined implicit conversions, ever.

## Ownership

- `iso` marks a unique handle to a *region* — the whole reachable graph moves
  as one unit (Verona's reading, not Pony's per-reference lattice). Channel
  send of an `iso` is a move; use-after-send is a compile error.
- Semantics ship before types: **copy-on-send now, `iso`-as-move later** with
  identical observable behaviour (transport.md). The type lands as an
  optimization, so no program breaks when it does.
- Borrows are **second-class**: passed down as parameters, never stored,
  returned, or captured by a spawn. Kills the lifetime-annotation apparatus;
  the rule "borrows flow down the stack only" is the entire discipline
  (Hylo / OCaml-modes precedent).
- `imm` (deeply immutable, shareable) with a one-way `freeze`: `iso` → `imm`
  is the build-privately-then-publish pattern.

## Concurrency surface

`scope` blocks own and join every task spawned inside;
`spawn(pin: core, restart: OnCrash)` exposes the topology instead of hiding
it; `within(30.s) { ... }` attaches deadlines that cancel real SQEs. A task
crash unwinds that task only, its in-flight CQEs are drained, then its arena
drops wholesale and the supervisor respawns — BEAM's layer 2 in miniature,
made sound by the memory model rather than by copying. ("Joined" and "dead"
both mean *drained*, per the reap rule above.)

## Compilation

Emit C (or QBE) — legitimate permanently (Nim precedent), and convenient here:
fixed stacks + no moving collector means no stack maps, so the C compiler's
opacity costs nothing, and FFI is free because the output *is* C. Monomorphic
except `option`/`result` at first. Self-hosting path in strategy.md.

## Taste sketch

```
fn serve(job: iso job) -> result<unit, serve_err> {
    let buf = bytes.alloc(64 << 10)          // task arena
    loop {
        let n = within(30.s) { job.conn.read(&buf) }??   // parks task, not core
        if n == 0 { return Ok(unit) }
        job.conn.write_all(&buf[..n])?
    }
}
```
