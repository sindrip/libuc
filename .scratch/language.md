# uc — language design conclusions

Provenance: an exploratory design conversation, 2026-08-20..22. `plan.md` and
its milestones are unchanged, and "the language is deliberately deferred" still
holds — this document is the answer-shaped sketch that fell out of pushing on
the question, recorded so the conclusions are not re-derived. Nothing here is
committed work; everything here was argued, and the rationale is the point.

Method, throughout: **derive, don't design.** Every construct below either maps
onto something the runtime/kernel already does (cited), or was deleted for
failing to. Kernel claims cite the pinned 7.2 tree in `out/src/`.

The name: **uc** — the language of libuc. Stdlib root `uc::`; module paths
transliterate to the C ABI (`uc::net::socket` ≡ `uc_net_socket`), so the
namespace tree _is_ libuc's exported symbol table.

---

## 1. The philosophy: exactly one way

The design's spine is a ladder of "exactly one way" decisions. Each deletion
was made deliberately; sugar is how options creep back in.

| dimension           | the one way                                  | what was deleted                                                                            |
| ------------------- | -------------------------------------------- | ------------------------------------------------------------------------------------------- |
| consume a sum value | `match` (total)                              | `try`, `?`, `or`, `if let`, `unwrap`, wildcards over enums                                  |
| iterate             | `for x := range it`                          | `while`, C-style `for`, hand-rolled recv loops                                              |
| wait on many        | `select` (deterministic, declaration order)  | randomized select, `default`-less polling variants (one `default` arm is the only try-form) |
| place work          | `spawn f(x)` = here; `s.spawn(f, x)` = there | `spawn on` syntax, SPMD, `layout` blocks, work stealing                                     |
| name a thing        | full path, one spelling program-wide         | `use`, aliases, re-exports, wildcards                                                       |
| render a program    | compiler-enforced formatter                  | style, config, two spellings of one AST                                                     |

Loop inventory: `if`, `for`, `match`, `select`, keywords `spawn` /
`return` / `break` / `continue`. `spawn` is the language's only special form
(takes an unevaluated call → the runtime's `fn + arg` pair). `yield` and
`crash` are **not** keywords — they are `uc::rt::yield()` / `uc::rt::crash()`.

---

## 2. The core: simple ADTs with total match

The language's pitch in one sentence: **simple ADTs with total match, on
cooperative fibers, over the ring.**

- `enum` with positional payloads; recursive via references (arena makes trees
  cheap). Representation: tag + union; niche packing derived where a payload
  has spare value space — `Option[&T]` onto null, `Result[u32]` onto the CQE
  sign bit, which makes match-on-Result compile to `if (res < 0)`.
- Patterns are **one level deep** — totality checking is a set comparison, not
  a decision-tree algorithm. Nested matches are written as nested matches.
- Over an enum, `_` is banned: every variant named. Adding a variant makes the
  compiler enumerate every decision site — **total match is the regression net
  this no-test-harness project otherwise lacks.** `_` remains legal over
  integers only.
- No guards, no or-patterns, no nesting, **no user generics**. The closed
  generic family: `Option`, `Result`, `chan`/`sender`/`receiver`, `map`,
  slices. A third parametric type gets argued in like a purity exception.
- Prelude = spelling, not a module: the two enums + the generic family.
- Mutability: `let` (immutable) / `var` (mutable) bindings; slices split into
  `[]u8` (read-only view, the default) and `mut []u8`. `mut` coerces to plain,
  never back. This is const-ness, not borrow checking.

Three eliminators, one per shape, each total: `match` (one value), `for range`
(one stream), `select` (many streams). `for` never hands you the Option; its
"None arm" is loop exit.

---

## 3. Errors

- Syscall truth is the ABI: CQE `res` is i32, negative = `-errno`. So
  `Result[T] = Ok(T) | Err(errno)`, zero-representation over ring ops. User
  error codes live above 4095 (the kernel reserves 1..4095).
- **The sticky-error law**: _Result appears in a signature only where the
  caller must branch._ Ops whose success payload and immediate error are both
  ignorable fold failure into the handle's state and surface it at a
  checkpoint. `Ok(_) => {}` in a diff is a review smell pointing at the API.
- Checkpoints are where errors are true: sockets surface stuck errors at the
  next `read` / explicit `finish()`; files at `sync()` — which is not a uc
  invention but the POSIX truth (write(2) success means "buffered"; fsync is
  where the disk answers). uc makes the fiction unwritable.
- No `errno`, no exceptions. `uc::rt::crash` is `[[noreturn]]` into the RT-007
  handler.

---

## 4. Memory and safety

**Two lifetimes** in the entire language: the fiber (arena, owned resources)
and the scheduler (per-scheduler `var`s, pools). Every reference is one or the
other.

Borrow checking, decomposed — one job deleted, one trivialized, one declined:

1. **Data races: deleted by topology.** Shared-nothing + cooperative
   single-threaded cores. No annotations; the guarantee is architectural.
2. **Use-after-free: collapsed to one rule at two points.** References never
   cross a fiber boundary; `spawn` and channel/`s.spawn` payloads are sendable
   **values** (moves/copies). Same rule, three justifications accumulated:
   sender-arena lifetime (same-scheduler), races (cross-scheduler), placement
   (`s.spawn`). Decidable at the call site, no lifetime inference.
3. **Aliasing+mutation: declined**, cushioned by runtime checks (bounds,
   overflow → crash handler) and by determinism — remaining bugs reproduce.

**No GC**, structurally:

- Fiber arenas, bulk-freed at exit (the mass case; compilers/parsers are the
  canonical fit).
- Self-managing containers: copy-in/copy-out APIs mean no external references
  into container internals, so slabs reuse eagerly and can't dangle anyone.
- Epochs for shared read-mostly snapshots: publisher-owned versions, freed
  once every scheduler's quiescence counter (a plain per-scheduler counter
  bumped each scheduler pass) advances past publication — RCU grace periods
  from a counter the loop maintains anyway.
- Same-core `Rc` (non-atomic — single-threaded cores make it cheap) as the
  rare escape valve.
- A GC would also be _unwelcome_: stop-the-world is preemption (invariant 7);
  a concurrent collector is io-wq-class background interference.

**Large binaries (`uc::mem::Blob`)** — the Erlang comparison sharpened this:

- The graph fact: binaries are **leaf objects** (no outgoing references), and
  refcounting's only correctness hole is cycles, which require outgoing edges.
  So refcounting is _complete_ collection for blobs — enforced by type
  (`[]u8`-shaped; a blob cannot store a reference).
- Erlang's actual pains are **discovery** (ProcBin references are implicit in
  process heaps; only a tracing GC pass notices they died → the "binary leak")
  and **contention** (atomic refc across schedulers). uc replaces discovery
  with **registration** (every reference is a recorded _lease_: fiber resource
  list / in-flight SQE / foreign-scheduler ledger — released by fiber exit /
  CQE / message) and atomics with **home-scheduler message decrements**
  (mimalloc's remote-free pattern; count is a plain int touched by one
  scheduler). Transfer-not-dup is the default on send, which deletes the
  distributed-RC race.
- Blobs are sealed-immutable; cross-scheduler reads need no synchronization.
  `loan.keep()` promotes a kernel buffer to a blob zero-copy;
  `conn.write(blob)` holds a lease until its CQE (the teardown rule and the
  blob lease are one concept). `IORING_REGISTER_CLONE_BUFFERS`
  (`io_uring.h:704`) supports cross-ring fixed-buffer use.
- The slogan: **Erlang traces to discover references; uc registers them at
  creation. The ledger replaces the collector.**

**Lent buffers** (`receiver[[]u8]` from streams): read-only by law. In classic
`PBUF_RING` mode the kernel doesn't touch a consumed buffer, but under
`IOU_PBUF_RING_INC` (`io_uring.h:889,899`) it retains the buffer and keeps
filling later regions, so mutability would be mode-dependent — the contract is
the lowest common denominator. Const fixes the space hazard; the loan's
lifetime (`done()`, fiber-exit backstop) fixes the time hazard — use-after-
`done()` races the next DMA fill and const does not help. This is `ring.h`'s
CQE rule (`ring.h:150-153`) promoted from completions to payloads.

---

## 5. Fibers — one concept, billed repeatedly

The fiber is the unit of: (1) failure — supervision, `die` observed by the
spawner; (2) resource scope — fds/conns auto-released at exit, which deleted
`defer` (no captures needed, works from any death depth, and exit-time CLOSE is
fire-and-forget — _better_ than a source-level defer that must suspend);
(3) checkout scope — `pg.acquire()` moves a conn in; clean exit returns-and-
resets it to the pool, death closes it (let-it-crash and connection hygiene are
the same mechanism); (4) allocation scope — the arena; (5) channel-endpoint
lifetime — last sender's death closes the channel (`None`), no `close` verb
exists; (6) generator — a fiber wearing a receiver is the lazy-sequence story;
(7) lease holder — blob references die with it.

Why `defer` specifically died: Go's defer compensates for resource scope ≠
concurrency scope. Here they are the same scope. Its three Go duties: unlock
(no mutexes exist), free (arena), close (fiber ownership).

---

## 6. Channels, streams, iterators

- One concept: `sender[T]`/`receiver[T]` over a bounded buffer (capacity ≥ 1;
  no rendezvous, no unbounded). Transport chosen by endpoint location: local
  queue (zero atomics) / cross-scheduler SPSC slot ring + doorbell / **kernel**
  — multishot accept is a `receiver[Conn]`, multishot recv a
  `receiver[[]u8]`, a timeout a ticker. The CQ ring is a channel the kernel
  sends on; the SQ is one we send on. The slot ring is `ring.c`'s data
  structure pointed at a sibling.
- No `close` verb: endpoints are fiber-owned; all-senders-dead ⇒ recv `None`.
  Send to dead receiver drops (Erlang's choice); the hang case is covered from
  the other side — an owner's death closes the `reply` slots it held, so
  requesters get `None`. Death propagates as channel closure; supervision and
  channels are one story.
- MPSC at any radius; MPMC same-core only. Cross-core competing receivers are
  work stealing smuggled through a channel — refused.
- `select` is deterministic (declaration order = priority, replay preserved;
  Go randomizes to hide starvation, uc keeps it visible). One `default` arm is
  the only non-blocking form. Select can range over I/O directly — the thing
  Go's hidden netpoller cannot.
- **The iterator protocol is the multishot convention**: `next() → Option[T]`;
  each CQE with `IORING_CQE_F_MORE` is a `Some`, the terminal CQE is the
  `None` (`io_uring.h:371,411`). `for x := range it` is its syntax. Slices /
  integer ranges / map keys present the same shape statically (compiler
  convention, no trait). Ranging a receiver can suspend; ranging a slice
  cannot — `nosuspend` blocks distinguish by type.
- `nosuspend { }`: inverse coloring — regions where any possibly-suspending
  call is a compile error. Marks the atomic regions instead of infecting
  signatures.

---

## 7. Scheduling and topology

- Cooperative only (invariant 7). Never preempted, no safepoints in loop
  backedges: a fiber that won't yield starves its core, and that is accepted
  as a bug class — deterministic, so it reproduces under the debugger.
  Runtime starvation detection is deliberately out of the language design.
- **The kernel vetoes migration**: `SINGLE_ISSUER` binds a ring to its task at
  setup (`io_uring.c:3065-3067`, enforced `register.c:764`); only the
  submitter completes requests (`io_uring.c:2242-2245`). A suspended fiber is
  physically owned by its ring's thread. Go's transparency rests on shared
  epoll + shared runqueues; completion-based pinned rings preclude it.
- **Boot contract: init creates exactly one scheduler** — the calling thread
  becomes it. As PID 1, `_start` is the caller; hosted, `uc_rt_init()`. Every
  additional scheduler is program text: `uc::rt::scheduler(cpu)` (a Result —
  granting can be refused). The runtime has **zero ambient concurrency**;
  a library that spawns threads at init is a rude guest, and incremental C
  adoption requires one-thread-at-a-time. `uc::rt::cpus()` reports the
  _granted_ set (affinity/cgroup mask), so `for cpu := range uc::rt::cpus()`
  is correct on bare metal and in a throttled container without options.
- Consequence: **the runtime kernel is finished at single-core.** Multi-core
  is stdlib code (`uc::rt::scheduler` calls clone/pin/ring-setup), invoked by
  `main` or never. Milestone 3 is a library function, not a runtime phase.
- `main` runs once, on scheduler 0 — the root of the spawn tree (supervision
  tree = fiber tree = Unix's init model). "All cores" is a for loop. Rejected
  on the way here: SPMD (`if core() == 0` is fork in a trenchcoat — structure
  decided by identity test), and a declarative `layout` block (a manifest;
  config-DSL, no joy). Identity tests for _routing_ (`owner == core()`) remain
  fine — data locality, not program structure.
- Placed spawn is a method, not syntax: `s.spawn(f, x)` — fn + one sendable
  value into the target scheduler's built-in inbox port. Lineage: X10
  `at (p) async`, Chapel `on`, Erlang `spawn(Node, F)`.
- Top-level `var` = **one instance per scheduler** (indistinguishable from a
  global in v0; each scheduler is a world at M3). `core var` and `shared`
  never became keywords. Cross-scheduler shared structures are `uc::rt`
  internals or rt types.
- **Teardown protocol** (hosted mode makes it mandatory): the hazard is that
  an in-flight op is a kernel-held reference into our memory — _nothing a
  pending SQE references is reclaimed until its CQE is observed_ (per-fiber
  in-flight count defers arena/stack reclaim; PBUF_RING's core-owned buffers
  exist so the common case pins nothing — plan.md already noted this).
  Cancellation is delivered as completion (`IORING_ASYNC_CANCEL_ALL/ANY/FD`,
  `io_uring.h:388-397`): fibers wake with `None`/`Err(ECANCELED)` and run
  their own exit paths — total match means every fiber has a written answer to
  "the stream ended". Stop flows down the tree, completion flows up
  (structured concurrency, no orphans). Ring close has `io_ring_exit_work`
  looping cancel-until-drained as backstop (`io_uring.c:2318,2349`).
  Scheduler 0 hosted: restore saved signal dispositions, disable sigaltstack,
  return the thread as found. Acceptance: an init/fini loop asserting fd
  count, RSS, threads, signal dispositions byte-stable.

---

## 8. The stdlib

Principle: **the stdlib is the opcode table, factored**, plus `uc::rt` for the
machine. Flat tree, two levels max (forced by full namespacing + the C ABI).
Modules ↔ kernel sources: `uc::net` ↔ net.c/cmd_net.c (+kbuf/zcrx internals);
`uc::fs` ↔ rw.c, openclose.c, fs.c, statx.c, xattr.c, sync.c, truncate.c,
splice.c; `uc::time` ↔ timeout.c; `uc::proc` ↔ waitid.c + clone/execve;
`uc::bytes`/`uc::str`/`uc::hash` are the language-adjacent floor.

Famous absences, each deliberate: no `uc::sync` (nothing to put in it), no
`uc::thread`, no `uc::syscall`/`os` (no escape-hatch module — invariant 1 at
the library layer), no allocator module. Protocols (http/redis/pg) are not
stdlib.

Milestone-2 minimum: `rt`, `net`, `time`, `bytes`, minimal `str`.

`uc::fs` conclusions: capability directories (`Dir` handles, `openat` dirfd
semantics, no cwd, no absolute-path API — WASI/cap-std convergence from the
ownership direction); intent constructors (`open`/`create`/`append`/`edit`)
instead of flag bitmasks; sticky writes with `sync()` as the checkpoint;
`stream()` = READ_MULTISHOT + loans; `list()` is the module's **disclosed
impurity** — no getdents opcode exists on 7.2 (verified: nothing in
`out/src/io_uring/`), so getdents64 is a purity-registry candidate.

The stdlib is where the laws become culture: every module API is reviewed
against sticky handles, Result-only-where-branching, streams-as-receivers,
loans, visible costs (e.g. `list()` returns names; `stat` is an explicit
second trip — no N-syscalls-per-entry surprises).

---

## 9. Namespacing and the formatter

- `::` walks modules, `.` walks values. No `use`, no aliases, no re-exports:
  every name is local, prelude, or absolute — **one spelling program-wide**.
  Name resolution is a three-case check; `grep` is a complete call-site query;
  the C ABI is a transliteration visible at every call site.
- One exemption: patterns go bare — fixed by the scrutinee's type, not chosen,
  hence not an alias. Construction pays full fare (`uc::rt::Down::Exited`).
- **The formatter is compiler-enforced**: concrete syntax is a bijection with
  the AST; formatting deviations are syntax errors; zero options. Layout is a
  pure function of the AST (no author-controlled rendering, no vertical
  alignment — diff radius beats columns). Enforcement is
  `print(parse(src)) == src` at build; editor format-on-save is the mercy.
  Comments need an anchoring model defined in the v0 spec (the one hard part).
  The printer is written with the first parser; the printer is the grammar
  spec. This is what makes full namespacing's grep-is-truth sound (no call is
  ever split mid-path).

---

## 10. Self-hosting

- Stage 0: uc compiler in hosted C23 in the toolchain container (the
  sanctioned hosted world, like GCC for the kernel — scaffolding, not
  identity). Stage 1: compiler in uc, built by stage 0. Stage 2: stage 1
  builds itself. Acceptance: **stage 1 output ≡ stage 2 output, byte-identical,
  SHA256'd** — requires deterministic codegen, hence: map iteration order is
  defined (insertion order), no ambient nondeterminism (already the law).
- **Self-hosting forces the libuc hosted mode into existence**: the compiler
  is a uc program and macOS has no io_uring, so it runs hosted in the Linux
  container — the first floor-plus-`query.c`-probe customer, needing only
  boring opcodes.
- Needs: `fs`, `bytes`, `str`, `map`, `hash`, arenas — no `net`. Feedback
  pressures: recursive ADTs (AST), arenas (per-unit, bulk-freed), the fmt
  writer, interning; optionally a parse→check→codegen fiber pipeline merged in
  name order.
- Codegen: aarch64 only, no LLVM, **emit static ELF directly** (no assembler,
  no linker, no exec). Frame pointers + line tables for lldb; the language's
  own bounds/overflow checks (→ `uc::rt::crash`) replace UBSan.
- The austerity dividend: canonical-form-only parser (bijection), one-level
  totality, closed generics, no inference ⇒ stage 0 is weeks-scale.
- Endgame acceptance stunt: the pinned VM rebuilds `/init` from its own source
  as a fiber tree and prints `SELFHOST OK <sha256>` on hvc0.

---

## 11. Kernel findings ledger (7.2, all verified in out/src/)

| claim                                                                                                                          | citation                                                          |
| ------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------- |
| opcode surface is libc-complete: files, net lifecycle, pipes, timers, futex, waitid, epoll_wait                                | `io_uring.h:256-320`                                              |
| `IORING_OP_BIND`/`LISTEN`                                                                                                      | `io_uring.h:312-313`                                              |
| setsockopt via URING_CMD (`SOCKET_URING_OP_SETSOCKOPT`)                                                                        | `io_uring.h:1041`, `cmd_net.c:182`                                |
| `IORING_OP_CONNECT`                                                                                                            | `io_uring.h:272`                                                  |
| REUSEPORT selection by 4-tuple hash                                                                                            | `net/core/sock_reuseport.c:527`, `net/ipv4/inet_hashtables.c:402` |
| MSG_RING wakes DEFER_TASKRUN rings via remote task work                                                                        | `msg_ring.c:68,113,93`                                            |
| MSG_RING carries 96 bits; can send a registered fd into a sibling's fixed table                                                | `io_uring.h:464-465`, `msg_ring.c:254,206`                        |
| SINGLE_ISSUER binds ring to creating task; only submitter completes                                                            | `io_uring.c:3065-3067,2242-2245`, `register.c:764`                |
| multishot streams: `CQE_F_MORE` = Some, terminal CQE = None                                                                    | `io_uring.h:371,411`                                              |
| kernel-owned buffer direction: PBUF_RING, READ_MULTISHOT(305), RECV_ZC(314)+ZCRX_IFQ(710), MEM_REGION(715), CLONE_BUFFERS(704) | `io_uring.h` registers 653-724                                    |
| incremental buffer rings: kernel retains buffer while chunks are out                                                           | `io_uring.h:889,899` (IOU_PBUF_RING_INC)                          |
| the in-kernel loop: `loop_step` as BPF struct_ops; kfuncs submit_sqes/get_region — the scheduler's loop step must stay BPF-sayable        | `loop.c` (`__io_run_loop`), `bpf-ops.c:17,25,251-267`             |
| `bpf_filter.c` is SQE deny-filtering (sandboxing for hosted code), not steering                                                | `bpf_filter.c:1-40`, `io_uring.h:724`                             |
| graceful cancel-by-criteria                                                                                                    | `io_uring.h:388-397`                                              |
| ring close cancels-until-drained (teardown backstop)                                                                           | `io_uring.c:2318,2349`                                            |
| **no getdents opcode** — fs::list() needs the registry                                                                         | grep of `out/src/io_uring/`: absent                               |
| WAITID = PID-1 reaping through the ring                                                                                        | `io_uring.h:306`                                                  |
| FUTEX ops = shared-structure waits stay ring-native                                                                            | `io_uring.h:307-309`                                              |

Purity-registry candidates discovered (each: no opcode exists): `getdents64`,
`execve` (for `uc::proc::exec`), `bpf(2)` (if REUSEPORT-eBPF steering is ever
wanted). To be argued in explicitly, per the registry discipline.

---

## 12. Rejected, with reasons (so they stay rejected)

- **`or`/`try`/`?` as error forms** — options; match-only won.
- **`defer`** — wrong scope (function ≠ resource lifetime here), needs
  captures (blocked on arena), incoherent with die-from-any-depth.
- **SPMD `main` + `if core()==0`** — fork in a trenchcoat.
- **`layout` declaration** — a manifest; neither code nor data; no joy.
- **Transparent scheduling / work stealing** — kernel-vetoed for blocked
  fibers (SINGLE_ISSUER); shared heap would reopen races+GC. The transparent
  Go-like layer is a _library discipline_ above `uc::rt`, not the language.
- **GC** — nothing to discover; see §4. Also architecturally unwelcome.
- **Unbounded mailboxes, selective receive, location transparency, hot code
  loading, built-in distribution** — Erlang imports declined; total match on
  message enums covers selective receive's legitimate uses at compile time.
- **Rust surface** — `&mut`/lifetime syntax writes checks the semantics won't
  cash; ceremony serves machinery we don't have. Kept: match-as-expression,
  `let`/`var` mutability marking.
- **The ring-native watchdog** (2026-08-22) — cross-scheduler tick
  surveillance, dropped everywhere (plan.md milestone 3 included). It was
  also the only cross-scheduler shared state besides the slot rings, so the
  designated-sharing ledger shrinks. Consequences accepted openly:
  starvation and cross-scheduler deadlock are undetected-at-runtime bug classes,
  deterministic and debugger-found; the epoch grace clock is an ordinary
  per-scheduler quiescence counter, not a watchdog byproduct.
- **Flag-word APIs** (`O_*` style) — options in a trenchcoat; intent
  constructors instead.

## 13. Prior art triangulation

**Erlang's semantics on seastar's engine, wearing Go's clothes, carrying
Rust's match.** Convergence with BEAM at the concurrency-semantics layer is
corroboration (they derived actors from fault tolerance, we from the target;
same fixed point). Every substrate axis is BEAM's dual: cooperative vs
preemptive (reduction counting), pinned vs migrating, arenas vs GC, native vs
VM, bounded vs unbounded mailboxes, addressed vs location-transparent.
NIFs are BEAM's hardest problem and uc's native mode; preemptive fairness is
BEAM's crown and uc's declared non-feature. Steal OTP's supervision
vocabulary (links, monitors, restart strategies) as _library_ when
supervision is designed.

## 14. Open questions / flags

- **Unit type**: `sync() Result[u32]` carries a dead payload; the prelude
  needs a unit answer. Dodged twice, filed.
- **fmt without generics**: one blessed variadic vs typed writer
  (`w.str().u32()`); writer is the "no fancy" default. Decide with `uc::str`.
- **Formatter comment-anchoring model**: in the v0 spec, day one.
- **Supervision/OTP vocabulary**: restart strategies, monitors — design when
  the fiber tree gets teardown (§7) and `uc::proc`.
- **Blob dup protocol — RESOLVED (2026-08-22, mechanized twice)**: the
  sketched naive +1/-1 counting is **unsafe even over FIFO rings** — an
  exhaustive model checker found the use-after-free (A dups, +1 rides A→H;
  A transfers the duplicate to B; B releases, -1 rides B→H; the -1 wins,
  count hits 0 under a live lease). Per-source FIFO never ordered the two
  rings. Adopted fix: **weighted refcounting** (dup splits the token's
  weight locally, no message; release sends its weight home) — verified
  safe by the same checker, and weight conservation proven inductive over
  every protocol step in F* (`Lease.fst`: zero count ⟹ zero live weight,
  so premature free is impossible at any bound). Known cost: a weight-1
  token cannot split — generous initial weight, refill round-trip in the
  rare exhaustion case. Spikes: `/tmp/uc-spikes` (ring-index arithmetic and
  the release/acquire barrier claims also verified there — `RingIndex.fst`
  discharged in F* on first attempt).
- **`splice`'s home** (`fs` vs a `pipe` module) — decide when it grows friends.
- **Slot-ring "slots freed" doorbell** (sender-side backpressure wake) —
  mechanism sketched, not designed.
- **`nosuspend` granularity** and whether the stdlib marks non-suspending
  guarantees in signatures or leaves it to the checker.

## 15. The v0 spec target

The single-scheduler language (§1-§6 minus cross-scheduler material) fits on a
page and targets milestone 2: its first program is the echo server, its
acceptance is the console, and every construct lands on runtime work the
existing tickets already point at (run queue, suspend-on-CQE, channels,
PBUF_RING, multishot). Multi-scheduler arrives later as `uc::rt` library
surface plus one semantic footnote (top-level `var` is per-scheduler) — the
language itself never changes again for cores.

```go
var hits u64

func main() {
    s := uc::net::socket(uc::net::TCP)
    s.bind(":7777")
    acc := match s.listen(64) {
        Ok(a) => a
        Err(e) => uc::rt::crash("listen: ", e)
    }
    for c := range acc {
        spawn serve(c)
    }
}

func serve(c uc::net::Conn) {
    for b := range c.stream() {
        hits += 1
        c.write(b)
        b.done()
    }
}
```
