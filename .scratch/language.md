# uc — deferred language design record

Status: **exploratory, consolidated 2026-08-30.** No language implementation is
scheduled. This file preserves decisions that were explicitly ruled in design
conversations and separates them from proposals. Runtime tickets do not depend
on language syntax; where this file names kernel behavior, `out/src/` remains
the authority.

The working name is **uc**, the language hosted by libuc. Its one-sentence shape
is: simple algebraic data types with total matching, running as cooperative
fibers over the ring.

## 1. One way

The design prefers one spelling for one semantic operation. This is stricter
than small syntax: a compact language with several equivalent idioms still
forces every reader and tool to choose among them.

Ruled decisions:

| dimension | one way | excluded alternatives |
|---|---|---|
| consume a sum or boolean | `match` | `if`, `try`, `catch`, `?`, `if let`, let-else, enum wildcards |
| iterate | `for x := range it` | `while`, C-style `for`, `loop`, generator syntax |
| wait on many | deterministic `select` | randomized selection and separate polling forms |
| place work | local `spawn f(x)`; method on a scheduler for remote placement | work stealing, SPMD, placement manifests |
| name a symbol | one full module path program-wide | imports, aliases, re-exports, wildcards |
| render an AST | one canonical printer | formatter configuration and author-controlled layout |

`yield` and `crash` are ordinary library calls, not keywords. `spawn` remains a
candidate special form because it receives an unevaluated call and lowers to an
entry point plus sendable argument.

### Match as the control primitive

Expression-position `match` is total. Enum variants must be named; `_` is
allowed only for open integer-like domains.

The guard-ladder use case survives without `if`: a statement-position match may
omit cases only when every written arm diverges. The remainder of the block is
the implicit continuation case.

```text
match invalid { True => return Err(Input) }
fd := match uc::fs::open(dir, path) {
    Ok(fd) => fd
    Err(e) => return Err(e)
}
```

A non-diverging partial match is rejected; `match flag { True => log() }` would
otherwise recreate `if` under another spelling. Whether divergence is expressed
through an uninhabited `Never` type or a narrower checker rule remains open.

Patterns stay one level deep in the first specification. No pattern guards,
or-patterns, or nested destructuring are assumed; nested decisions use nested
matches. That keeps totality checking a set comparison.

### Loops

`for range` is the only looping construct. Iterator adapters may build another
iterator, but consumers such as fold, search, count, or collection are written
as `for` rather than duplicated as eager combinators.

Loop bodies produce a closed `ControlFlow[Break, Continue]` value. This records
the ruled decision that break/continue are an ADT; the exact single exit keyword
and state-threading syntax remain open. There is no guaranteed tail-call
optimization, so recursion is not a second general iteration mechanism.

## 2. Types and errors

The core has positional-payload enums, structs, immutable `let`, mutable `var`,
and read-only versus mutable slices. Mutability is constness, not a borrow
checker.

The initial generic family is closed and compiler-known: `Option`, `Result`,
`ControlFlow`, channels, slices, and maps. User-defined generics, closures, and
capture semantics are deferred rather than implicitly assumed by library
examples.

Errors are values:

```text
Result[T] = Ok(T) | Err(Error)
```

The ring's signed 32-bit CQE result makes many syscall results cheap to lower,
but it does not provide a universal niche for every `u32` payload. Representation
is derived per instantiated result domain; full-range values retain a distinct
tag when necessary.

The sticky-error rule remains a design test: a `Result` belongs in a signature
when the caller must branch now. Operations whose immediate outcome is not
actionable may retain failure in a handle and surface it at a real checkpoint,
such as file synchronization. Dropping a non-unit value as a statement is a
compile error.

The language surface has no ambient `errno`; libuc's C ABI still does, per
fiber. Exceptions, unchecked unwrap, and failure-propagation sugar are absent.

Short-circuit boolean syntax is unresolved. Strict boolean operations plus an
explicit match are consistent with the one-way rule. Thunk-based operators are
not a proposal until closures exist.

## 3. Memory boundary

The intended default lifetimes are fiber-scoped arenas and scheduler-owned
long-lived pools. This is a direction, not a proof of memory safety.

Shared-nothing removes races only for state owned by one scheduler. Explicit
cross-scheduler mailboxes, registries, or immutable shared blobs still require
publication and reclamation protocols. Multiple schedulers may share a CPU;
the ownership unit is never the core.

Proposed first safety boundary:

- references do not cross fiber or scheduler boundaries;
- spawn and channel payloads are sendable values or typed handles with an
  explicit transfer/loan protocol;
- bounds and arithmetic checks remain enabled and fail through the runtime;
- aliasing mutable memory is not claimed safe merely because scheduling is
  cooperative.

Large immutable blobs may eventually use home-scheduler accounting and message
based release, but no refcount protocol is committed here. The former weighted
refcount sketch and external temporary proofs are historical experiments, not a
repository-backed specification.

## 4. Fibers as resource scope

The fiber is intended to align failure, allocation, owned descriptors,
connection checkout, channel endpoints, and operation leases. That alignment is
why a general source-level `defer` was rejected: resource teardown belongs to
the fiber scope rather than whichever function frame happened to acquire it.

Automatic teardown is not fire-and-forget reclamation. On fiber exit, operations
that reference fiber-owned memory are cancelled and drained; the fiber remains
a zombie until every terminal CQE and required notification has arrived. Only
then may its stack, arena, descriptors, and operation records be recycled.

Supervision, restart policies, links, monitors, and join semantics are deferred
until the runtime has the cancellation/zombie machinery in UC-017.

## 5. Scheduling and topology

The runtime is cooperative. A fiber that never suspends can starve its scheduler;
this is an accepted program bug, not a hidden preemption point.

Boot creates scheduler zero by making the calling Linux task a scheduler. An
additional scheduler is explicit program/library action: create a Linux task,
place it, and have it become a scheduler on itself. `main` runs once on the root
fiber of scheduler zero.

Fibers never migrate. The kernel enforces that a ring's single issuer is its
submitting Linux task (`out/src/io_uring/io_uring.c:3065-3067` and
`out/src/io_uring/tctx.c:198-204`); the broader fiber rule is the runtime's
design, required by scheduler-owned stacks, arenas, buffers, wait queues, and
completion routing. A ready fiber without kernel work is still not eligible to
move.

Placed spawn is a method on a scheduler handle and sends one sendable argument
through that scheduler's inbox. Work stealing and competing cross-scheduler
receivers remain excluded.

## 6. Channels, streams, and iterators

The common pull shape is a fallible iterator:

```text
next -> item | end | error
```

Its owner may destroy it early; destruction cancels and drains any kernel work
before resource reclamation. A receiver is an iterator with a sender side,
while a kernel source such as accepted connections or received byte chunks has
only the iterator side. The C runtime's UC-020 groundwork uses the same shape,
but this does not commit language spelling or a generic C value representation.

- scheduler-local channels use ordinary queues;
- cross-scheduler channels use explicit mailbox transport;
- operation records route kernel completions; typed iterators expose repeated
  values from them;
- `select` waits on several receivers in declaration order;
- `for x := range receiver` consumes until the iterator terminates.

The io_uring terminal rule must remain exact. `IORING_CQE_F_MORE` means another
CQE follows the current one (`out/src/include/uapi/linux/io_uring.h:515-533`).
A CQE with `F_MORE` clear is terminal but may still carry a final value or error.
The iterator therefore delivers a terminal value when appropriate and records
local end afterward; it never equates “terminal” with an empty payload.

Every receiver has explicit capacity and overflow policy. A semantic iterator
does not promise a kernel opcode: unmetered accept/poll needs bounded
single-shot rearming unless it deliberately chooses a lossy or fatal overflow
policy; provided-buffer receive gains a real credit bound. Cross-scheduler MPMC
is work stealing through a data structure and is not offered. Endpoint death,
closure, send-to-dead behavior, and backpressure wakeups remain part of the
future channel specification.

Lent kernel buffers are read-only while exposed. Returning a loan ends the
program's right to access it; incremental provided-buffer mode may allow the
kernel to keep filling the same buffer after partial delivery.

## 7. Names, formatting, and comments

`::` walks modules and `.` walks values. There are no imports, aliases,
re-exports, overloads, implicit conversions, or wildcard module paths. One
module maps to one file and a module path transliterates predictably to its C ABI
symbol.

Formatting is canonical, not merely idempotent:

```text
parse(a) == parse(b)  implies  print(a) == print(b)
```

The compiler accepts only the canonical rendering. Line breaks, blank lines,
trailing commas, and ordering are printer decisions rather than source-level
style. This makes migrations typed-AST rewrites followed by canonical printing.

Free comments are banned. One doc-comment form attaches to declarations by a
fixed rule; parameters, symbol links, and examples are checked. TODOs and lint
exceptions are structured attributes with required reasons. The exact set of
declaration nodes that may carry documentation remains open.

## 8. Evolution and agent-facing tooling

Language evolution follows the one-way rule. A deprecation is either a typed
AST rewrite or a diagnosed breaking change; old and new forms do not remain
indefinitely legal alternatives.

The compiler mode for a version transition parses the older grammar, checks it,
rewrites machine-applicable changes, prints current canonical source, and emits
stable diagnostic IDs for the remainder. Published source is version-pinned and
the package corpus is migrated before a release.

LLM-friendly means low choice plus a strong local oracle, not terse syntax:

- the specification and core reference fit in a context window;
- everything needed at a call site appears in the signature or file;
- diagnostics are structured, stable, and include canonical fixes;
- every example compiles and runs;
- generated reference documentation derives signatures and error variants;
- removed familiar constructs produce targeted diagnostics explaining the one
  supported form.

## 9. Kernel-derived constraints

These are inputs, not a claim that the opcode table is a complete libc:

- `IORING_OP_BIND`, `LISTEN`, `PIPE`, `WAITID`, and the futex operations exist
  in the pinned UAPI (`out/src/include/uapi/linux/io_uring.h`).
- `MSG_RING` delivers values and can wake a `DEFER_TASKRUN` target; mailbox
  memory itself is a userspace ownership protocol.
- multishot, zero-copy notification, and mixed-CQE flags require operation
  identity rather than a bare fiber pointer.
- no io_uring opcode has been found for `getdents64`; that does not authorize a
  direct syscall. Every addition to the direct-syscall list requires the
  invariant discussion in AGENTS.md.
- ring release invokes the kernel's cancellation/drain path
  (`out/src/io_uring/io_uring.c:2422-2448`), but hosted libuc teardown still
  needs an explicit userspace lifecycle and cannot delegate correctness to file
  release.

## 10. Open specification questions

- `Never` versus a narrower divergence rule for partial statement matches.
- Exact `ControlFlow` syntax and how nested-loop exits compose.
- Strict boolean operations versus dedicated short-circuit syntax.
- Which iterator adapters are compiler-known without user generics.
- Unit type and the unused-value rule for single-inhabitant values.
- `select` arm typing, closure, and backpressure behavior.
- The doc-comment attachment set and shadowing rules.
- Supervision vocabulary after cancellation and zombie lifetime exist.
- Whether this language remains the right abstraction after libuc hosts its
  first real vendored C libraries.
