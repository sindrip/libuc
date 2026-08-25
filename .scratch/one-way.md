# uc — one way: control flow, tooling, documentation

Provenance: a sidebar in an unrelated-repo session, 2026-08-25, hence no rt git
context around it. Conversation-derived; nothing here is verified against code.
`language.md` (2026-08-20..22) was read *after* the conversation, and §9 below
compiles the deltas against it — several of today's answers assumed features
that document deleted, and those are marked as conflicts, not conclusions.

Three tiers, kept apart throughout:

- **Ruled** — Sindri's words today. Stands unless he changes it.
- **Built on** — sharpenings of a ruling from the conversation, not themselves
  ruled.
- **Proposal** — suggested in the conversation; several conflict with
  `language.md` laws and need a ruling before they mean anything.

The theme (ruled): **what Python thought it wanted to be.** Not "the pythonic
way" — literally the way. If the way must change, that is a deprecation, and a
deprecation ships with an auto-fixer. The motive (ruled): make the language
LLM-friendly; every decision below was checked against that.

---

## 1. `match` is the only destructor — and no `if`

Ruled: `match`, total, is the only eliminator of a sum value. Rejected by name:
`try`, `catch`, `?`, `orelse`, `if let`, let-else, macros — and `if` itself,
which `language.md` §1 still lists. `if` is match over `Bool`; keeping it is a
second spelling of one AST.

Every deleted construct — `?`, `try`, `catch`, `orelse`, `if let`, let-else,
`do` — is one thing: **a match where one arm is the rest of the block.** The
design gives `match` that shape once, in one direction, and needs none of them.

### The guard form (ruled in shape; the typing rule is built on)

Sindri's target: the guard ladder, `if (a) { early_return }` repeated, reads
well and must survive. It survives as a statement-position match whose written
arms all diverge, with the unmatched cases continuing:

```go
match a { True => return E1 }
match b { True => return E2 }
fd := match uc::fs::open(dir, path) {
    Ok(fd) => fd
    Err(e) => return Err(e)
}
match fd.kind() { Dir => return E3 }
```

Rule: **a statement-position `match` may be non-exhaustive iff every written
arm diverges.** The remainder of the block is the implicit arm. Divergence =
`return`, the loop exit, `uc::rt::crash`, a call to a noreturn function.

Why the divergence requirement is load-bearing: `match a { True => log() }` is
rejected — a non-diverging single arm would be `if`-without-`else` in disguise.
Early exits get the elision; side-effect-only conditionals stay fully spelled
(`True => log()  False => {}`), and that shape is rare in expression-oriented
code and usually a boolean-blindness smell (return the evidence, not a `Bool`).
The checker can also flag a guard whose written arms are exhaustive: the
remainder is dead, which `if a { return } else { return }` never told anyone.

How divergence is known is open: a `Never` (uninhabited) type makes it a typing
rule and also gives arm-omission for uninhabited payloads; a purely syntactic
check ("arm ends in return/exit/crash") needs no new type. `language.md` has
no bottom type — `uc::rt::crash` is `[[noreturn]]`. Decide with the v0 spec.

### Binding the payload (ruled: not the `let` form)

The continuation-arm construct has a second orientation — spell the binding
pattern, leave the exit implicit (`let Ok(v) = r  Err(e) => return e`). Sindri
doesn't like it. Standing answer: **the explicit two-arm expression match**,
shown above. Residue: the identity arm `Ok(fd) => fd`, one line per fallible
call.

It is uniform, which is Go's smell, but differs where it matters: expression-
positioned (the call sits inside a larger expression), the error arm varies
per site without changing shape, and the arm does real work (projects the
payload) where `if err != nil { return err }` re-checks what the type already
said. There is no third option — the payload is named by a pattern in the
remainder or in an arm; the only way around both is flow-sensitive narrowing,
rejected (implicit, needs a sometimes-legal projection, breaks under mutation).
A bodiless-arm spelling inside the match block (`Err(e) => return e  Ok(fd) =>`
with the remainder as body) was offered as an alternative if the objection is
to spelling rather than idea. Undecided.

### Infallible operations

The general answer — uninhabited error type + arm omission — is unnecessary
here. `language.md` §3's sticky-error law already says *Result appears only
where the caller must branch*; an infallible operation doesn't return `Result`.
Refining input types (`NonEmpty`, bounded index) so operations can't fail is
the same law from the other side. No construct needed.

### Why "you can't forget to check" is stronger than it sounds

In Go the value exists independently of the error: `x, err := f()` yields a
usable zero-value `x` whether or not `err` is read. Forgetting the check isn't
the bug; `x` already existing is. Here the payload doesn't exist until a
pattern binds it, and there is no free-floating `err` to check wrongly after
shadowing. Total match adds what Go can't have: forgetting a *variant* is a
compile error. What remains are written escape hatches, and they need closing
so "nothing by omission" holds:

- Dropping a non-unit value (`f()` returning `Result`, as a statement) is an
  error. (Proposal — the same rule closes `a && g()` as a stealth `if`, and
  bears on `language.md` §14's unit-type flag; see §9.)
- `_` over enums is already banned by `language.md` §2 — stronger than the
  lint suggested today; the ban stands.
- `uc::rt::crash` with a required reason string (proposal, §6).

---

## 2. Short-circuiting — undecided, dependency noted

Sindri asked whether `&&`/`||` short-circuit. Today's answer — they are
`and_then` on `Bool`, ordinary functions taking a thunk for the right operand,
result must be used so `a && f()` can't be a statement — **assumes closures and
generics**, which `language.md` §2 deletes (closed generic family; captures
blocked on arena). Under those laws the choices are:

- builtin `&&`/`||` as match sugar on `Bool` — two more spellings of one AST;
- strict `and`/`or` functions, with `match` for ordered evaluation.

In favour of strict: the motivating case (`p != null && p.x`) doesn't exist —
the null check is a pattern — and patterns keep eating the rest
(`v.len() > 0 && v[0] == 'a'` is a slice pattern, *if* nested patterns exist;
they don't in `language.md` §2). When the right side is effectful or expensive
the order dependence should be visible, and a match makes it visible. Needs a
ruling.

---

## 3. Exits and loops

### `return` is a keyword (asked; answered yes, uncontested — built on)

The guard form only works because arms can diverge; `return` is the primary
way they do. It can't be a function — it's a non-local exit, and the
alternatives that make exits values (continuations, effects) are a far larger
commitment. Design points (built on):

- Last expression is the function's value; `return` is only for early exit.
  Every `return` in the source is then a guard, and a trailing `return` is a
  lint.
- `return` is local to the nearest `func`. (Non-local return from closures is
  moot without closures.)
- Labeled exits rejected: nested-loop exit propagates as the inner loop's
  value (§3, below) — one extra arm, same mechanism as everything else.

### `break`/`continue` are an ADT (ruled)

Sindri: control like break/continue should be an ADT. Yes for the value, no
for the jump — two different things:

- **The value.** A loop body evaluates to `ControlFlow[B, C] = Break(B) |
  Continue(C)`, and the loop matches on it: exhaustion yields `Continue(final)`,
  early exit yields `Break(b)`. The body's type is the loop's type.
- **The jump.** The ADT says what a body evaluates to; it doesn't let you leave
  from the middle, and the guard ladder inside a loop needs to. Body is a
  block, not a closure, so `return` reaches the function; the loop exit is one
  keyword that takes the `ControlFlow` value (`break v` and `continue c` as
  separate keywords would be two spellings of it — built on, not ruled).

`ControlFlow` is a third parametric type. Under `language.md` §2 it gets argued
in like a purity exception, or joins `Option`/`Result` as prelude spelling.

### Iterators are the only loop (ruled; `language.md` §1 already had it)

`for x := range it` is the one construct. `while`, C-style `for`, `loop`,
do-while: all iterators — `forever()`, `take_while`, ranges, a stack's
`drain()`. The property that makes it one construct rather than one keyword
with three syntaxes (Go): **base iterators step, they don't loop** —
`forever().next()` is `Some(())`. Only `for` loops; `filter`'s `next` is a
`for` over its inner iterator. `select` is not a loop and survives as the
many-streams eliminator (`language.md` §6).

Recursion isn't a loop: no tail-call guarantee, so it can't iterate a sequence
without a stack bound. It's for data whose shape is recursive, depth = the
structure's depth. Declining TCO is what makes "one loop" honest.

**The real fork is loops versus combinators** — where Python fails (`for`,
comprehensions, `map`/`filter`, generators, all doing one job). Rule (built
on): *adapters build iterators; `for` is the only consumer.* `map`, `filter`,
`zip`, `enumerate` are lazy and return iterators; `fold`, `sum`, `find`,
`any`, `count`, `collect` do not exist as methods — each is a `for`. For that
to hold, `for` carries state without mutation:

```go
total := for x := range xs with acc := 0 { Continue(acc + x) }
first := for x := range xs { match p(x) { True => Break(x)  False => Continue(()) } }
```

Falls out: a search is `ControlFlow[T, ()] ≅ Option[T]`; Python's `for … else`
is the `Continue` arm; no `var acc`. A break-less body has an uninhabited
`Break` side and a stateless one has `ControlFlow[·, ()]` with a single
inhabitant — make single-inhabitant types exempt from the unused-value rule
generally and the statement form needs no special case (bears on
`language.md` §14 unit-type flag).

Adapters are library code over the `next() → Option[T]` protocol; how much
of the adapter set survives without user generics is §9's problem. No
generators/`yield` — `language.md` §5 already has the answer (a fiber wearing
a receiver).

---

## 4. Deprecation with an auto-fixer (ruled)

One-way plus change means there is never a period where two forms are legal
by design. Sindri: deprecate; between versions ship an auto-fixer; for the
non-mechanical remainder ship LLM instructions — simple in the agentic era.
The three data points: Go's `go fix` worked (syntactic, typed changes);
Python's `2to3` failed (`str`/`bytes` undecidable from source); Zig breaks
every release with no fixer and the ecosystem eats it. Constraints that make
the promise credible (built on):

- **Every deprecation is a rewrite over the typed AST, or it isn't a
  deprecation.** Undecidable given types ⇒ different language, not a version
  bump. The fixer is a compiler mode (it needs the checker), not a tool.
- **No silent semantic changes.** Behaviour change ⇒ syntax change, so the old
  form is findable. Same-syntax-different-meaning is invisible to a fixer.
- **Parse for longer than you compile.** N warns and offers the fix; N+1
  rejects; the N grammar stays behind `--from=N` for as long as upgrading
  from N is supported. Grammar stays; checker refuses.
- **Two tiers, marked in the compiler.** Machine-applicable fixes go to the
  fixer; everything else is a diagnostic with a stable ID and a migration note
  *attached to the deprecation itself* — `@deprecated(since, id, note)` — so
  the LLM instruction can't drift from the thing it describes. The agent loop:
  fixer, compile, read structured remainder, apply, compile until green. That
  is "simple" only because the compiler supplies a finite enumerated site list
  and a pass/fail oracle.
- **Source-only registry.** Every published package ships source; before a
  release the fixer runs across all of it (Rust's crater). That's the
  migration's test suite.
- **Version pinned per project**, in the manifest. Unlike Rust editions, old
  versions aren't compilable forever — only for the fixer window.
- The stdlib deprecates with the same attribute as everyone else.

---

## 5. The formatter is canonical, not merely idempotent (ruled; sharpened)

`language.md` §9 already has compiler-enforced formatting with
`print(parse(src)) == src`. Sindri's framing today: since not using the
formatter is a compile error, `format(src) ⇔ format(format(src))` and there's
no ambiguity. Sharpening (built on): idempotence is satisfied by formatters
that preserve user layout (gofmt keeps line breaks inside expressions and
blank lines; Black's magic trailing comma). The property that eliminates
ambiguity is **canonical**: `parse(a) = parse(b) ⇒ format(a) = format(b)` —
every AST has exactly one spelling. Implies idempotence, not the reverse, and
means the formatter honours *no* user choice: not line breaks, trailing
commas, blank lines, or import order (there are no imports — `language.md` §9).

What it buys:

- The fixer is `parse_N → AST_N → migrate → AST_{N+1} → print_{N+1}`. No
  concrete syntax tree, no trivia preservation, no overlapping text-span
  edits — rustfix's entire difficulty is gone. `migrate` is a match over
  `AST_N`, so **a deprecated constructor without a rewrite is an exhaustiveness
  error in the compiler's own source** — "every deprecation has a rewrite"
  becomes a type error, not a policy.
- Every diff line is a semantic change. Generated, hand-written, and
  LLM-written code are indistinguishable after normalisation; agents need no
  style guide.
- Hash of source = hash of AST: content addressing for registry and build
  cache.
- Formatter output is part of the language version; changing it is a
  deprecation applied by the fixer as one whitespace-only commit.

The cost: blank lines carry no information — forbidden or inserted by rule
(one between top-level items, none inside blocks). Grouping is structure
(modules, nested blocks), which is at least honest about being structure.
Comments were the other hole; §6 closes it.

---

## 6. Comments are banned; doc comments only, single form (ruled)

Resolves `language.md` §14's open "formatter comment-anchoring model" flag by
deletion: doc comments attach to a declaration by one fixed rule (always
preceding what they document; a file's first doc comment attaches to the
module node), so there is no free-floating placement and source ↔ AST is a
true isomorphism.

The stronger argument is correctness: free comments are the only text the
compiler doesn't check, hence the only text that can be wrong, and a model
believes a stale comment over the code next to it. Doc comments here are
checked (params, links, examples), so **every line in a file is verified by
something.** The bet: every inline comment is a failed name or a failed
decomposition. Mostly true in practice; the ban makes it a compile error.

What the ban displaces (built on):

| displaced                                 | goes to                                                                                                                                                                                                                 |
| ----------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| inline "why" at a tricky line             | extract a named `func` and document it; or doc comments allowed on any *declaration* node — items, fields, variants, params, `let`/`var` — never on expressions/statements. Explain by naming, enforced by where docs can go |
| `// SAFETY:` / `// unreachable because …` | required argument: `uc::rt::crash("hw counter is 1-based")`, `@allow(lint, "reason")`. Presence compiler-checked; text prints when the check fires                                                                     |
| TODO/FIXME                                | `@todo("…")` attribute: a warning, enumerable by the toolchain, promotable to error at release                                                                                                                          |
| commented-out code                        | version control                                                                                                                                                                                                         |
| section headers                           | already dead with canonical blank lines; grouping is modules/blocks                                                                                                                                                     |
| license / file header                     | module doc comment                                                                                                                                                                                                      |
| derivations, diagrams, paper refs         | ADR, linked from the doc comment; link resolved and checked like a symbol                                                                                                                                              |

Schema, not prose: required one-line summary, optional structured sections,
tested examples. Public undocumented item is an error; private items may be
undocumented but use the same form if documented. Exactly one syntax — not
Rust's `///` / `//!` / `/** */`.

---

## 7. LLM-friendly as the design target (ruled)

The framing that makes the rest fall out: **LLM-friendly isn't terse; it's
low entropy per token plus a strong local oracle.** Boilerplate is cheap for a
model when it's deterministic (Go's `if err != nil` is trivial to emit). What's
expensive is *choice* — which of five idioms, which inferred type, what a macro
expands to. One-way, canonical form, no macros, total match all attack choice,
not length. Implications beyond what `language.md` already has (built on):

- **The spec fits in the context window.** A new language has no pretraining
  corpus; it is learned in-context from spec + stdlib reference. Budget for it
  — language + core library under a few tens of thousands of tokens — and every
  feature pays its way in spec size. Every second way is spec you can't afford.
- **Everything a call site needs is in the signature or the file.** Models read
  a window. `language.md` §9's full paths and §2's no-inference already do most
  of this; add: no overloading (one name, one meaning), no implicit
  conversions, and a shadowing ban — same name meaning two things inside a
  window is a top source of model mistakes.
- **Diagnostics are the API.** Structured, stable IDs, full types printed,
  machine-applicable fixes marked. Compile speed matters *more* for agents than
  humans — they iterate more times per hour; sub-second incremental is a
  requirement.
- **The corpus never goes stale.** The payoff of §4 nobody else has: all
  published code is canonical-form and mechanically upgradeable, so every
  example in existence can be brought to the current version. Old-version code
  in training/context is the perennial problem (Python 2, Swift 3, editions).
- **One test construct, in the language.** Deterministic output, no framework
  choice, doctests that run.
- **One module = one file, path is name** — already `language.md` §8/§9; noted
  because Rust's `foo.rs`/`foo/mod.rs` duality specifically trips models.
- `uc::rt::crash` stays *checked* in every build. An "assume" mode is UB, and
  UB is the least LLM-friendly semantics there is — the model can't reason
  locally about what happens.

---

## 8. Documentation is LLM-friendly (ruled; details built on)

Same principle: optimise for lookup precision and never being wrong, not for
narrative.

- **Derive everything derivable.** Signatures, error variants, what a function
  consumes — from the compiler, never retyped. Doc comments have a schema
  checked by the compiler: nonexistent param ⇒ error; unresolved symbol link
  ⇒ error (rustdoc intra-doc links, mandatory).
- **Every example compiles and runs**, in canonical form, upgraded by the
  fixer each version. The reference for version N is version-N code.
- **One document per concept; no duplication.** Contradiction is worse than
  absence for a model — it picks one at random. Three artifacts with disjoint
  jobs: the spec (normative; written as the system prompt it will be), the
  reference (per-symbol, generated), ADRs (why).
- **Document what doesn't exist.** Models bring `while`, `if`, `try`, `null`,
  `.fold()` from every other language. Reserve those as keywords whose *only*
  purpose is a targeted diagnostic ("no `while`; use `for x := range …`") —
  documentation delivered where the agent is reading. Same for stdlib names
  from Rust/Go/Zig/Python via a did-you-mean table.
- **Diagnostics are the primary channel.** Every error ID has a stable page
  (`--explain`), printable inline: rule, one-sentence why, canonical fix. The
  spec is the reference; the errors are the tutorial.
- **Offline, CLI-addressable, token-budgeted.** `uc doc uc::net::socket` prints
  one symbol in fixed markdown, deterministic, no network — sandboxed agents
  have the toolchain and nothing else. Plus one full dump (spec + reference)
  sized to a context window.
- **Vocabulary is documentation.** One verb per concept across the stdlib
  (`len`, never also `size`/`count`). Fully spelled identifiers.
- **Human tutorials are generated** from the tested examples; they can't drift.

---

## 9. Deltas against `language.md`

| `language.md`                                                             | today                                                                                                                | status                                                                                                              |
| ------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| §1 loop inventory keeps `if`                                              | `if` deleted; guard form + two-arm match replace it                                                                  | **superseded by Sindri today**                                                                                      |
| §1 keywords `break` / `continue`                                          | one loop exit taking a `ControlFlow` value; loop evaluates to the `ControlFlow`                                      | **ruled** (ADT); the single-exit-keyword detail is built on. `ControlFlow` is a third parametric type — argue in     |
| §1 `for range` is the only loop                                           | confirmed; `select` survives as the many-streams eliminator, not a loop                                              | confirmed                                                                                                           |
| §14 formatter comment-anchoring flag                                      | free comments banned; doc comments single form, attached to declarations                                             | **resolved (ruled)**                                                                                                |
| §9 formatter is a bijection, `print(parse(src)) == src`                   | sharpened to canonical: `parse(a)=parse(b) ⇒ format(a)=format(b)`; blank lines carry no information                  | consistent, sharpened                                                                                               |
| §5 `defer` deleted; fiber = resource scope                                | today's "open item: linear types for cleanup"                                                                        | **retracted** — already answered by §5                                                                              |
| §3 `Result[T] = Ok(T) \| Err(errno)`, zero-representation over CQE      | proposal: structural/inferred error sets, `e @ Err(_) => return e` without rewrapping, exhaustive match over error set | **conflict** — §3 is kernel-derived; stands unless ruled. The "cheap residual arm" claim depends on it              |
| §2 patterns one level deep; no guards, no or-patterns                     | proposals: nested patterns, guards, or-patterns, tuple scrutinee, `@` bindings                                        | **conflict** — one-level totality was chosen for a set-comparison checker; today's §2 slice-pattern remark depends on it |
| §2 no user generics, closed family; captures blocked on arena             | combinators-as-library, thunk-based `&&`/`||`, `from_fn(closure)`                                                     | **conflict** — if §2 stands, short-circuit (§2 here) and the adapter set (§3 here) need redoing                       |
| §2 `_` banned over enums                                                  | wildcard-arm lint                                                                                                    | `language.md` is stronger; ban stands, lint subsumed                                                                |
| §2 no inference                                                           | "inference inside bodies, explicit signatures"                                                                       | consistent — `:=` is the only local inference either way                                                            |
| §14 unit-type flag                                                        | single-inhabitant types exempt from unused-value rule; `ControlFlow[·, ()]` as the loop's statement type             | bears on the flag; not a resolution                                                                                 |
| `uc::rt::crash` noreturn                                                  | stays checked in every build; required reason string                                                                 | consistent; reason-as-argument is a proposal                                                                        |
| `select` (§6)                                                             | never entered today's exit/`ControlFlow` discussion                                                                  | **gap** — do select arms need the guard form's divergence-or-value typing?                                          |
| —                                                                         | deprecation + fixer + LLM notes (§4 here)                                                                            | new, ruled                                                                                                          |
| —                                                                         | LLM-friendly target (§7), docs regime (§8)                                                                           | new, ruled in principle; bullets are built on                                                                       |

---

## 10. Rejected today, with reasons

- **`if`** — match over `Bool`; a second spelling. The guard form keeps what
  people liked about it (the else branch is the remainder of the function).
- **`try` / `catch` / `?` / `orelse` / `if let` / let-else** — all the same
  continuation-arm construct; the design has it once, and Sindri doesn't want
  the binding-oriented spelling.
- **Macros** — surface syntax ≠ semantics; the model can't reason from text.
  Rust's `try!` history (macro over match, later promoted) shows the pressure
  they'd absorb; without them that pressure lands on §1's guard form, which is
  first-class syntax for exactly that reason.
- **Labeled break** — nested exit is the inner loop's value; one arm.
- **Flow-sensitive narrowing** — implicit; needs a sometimes-legal projection;
  wrong under mutation and aliasing.
- **Guaranteed tail calls** — would make recursion a second loop.
- **Generators / `yield`** — second way to build an iterator, and a coroutine
  transform; `language.md` §5 already has "a fiber wearing a receiver".
- **Free comments** — the only unchecked text in a file.
- **`unreachable`-as-assume** — UB; unreasonable-about locally.
- **Rust editions' "old form stays legal"** — the two-ways failure with a
  version number on it. Fixer window instead.

## 11. Open

- **`Never` type vs syntactic divergence check** for the guard form (§1).
- **Payload binding spelling**: two-arm match stands; bodiless-arm alternative
  recorded (§1).
- **Short-circuit**: strict `and`/`or` vs builtin sugar; blocked on the
  closures/generics ruling (§2, §9).
- **How much of the adapter set survives** without user generics (§3, §9).
- **`ControlFlow`'s admission** — prelude spelling or purity-exception (§3).
- **`select` arms** under the exit/`ControlFlow` model (§9 gap).
- **Doc-comment attachment set** — items only, or all declaration nodes
  including `let`/`var` (§6).
- **Shadowing ban** and what it means for `:=` re-declaration (§7).
