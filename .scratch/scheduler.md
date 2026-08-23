# The scheduler — destination public interface

Status: conversation-derived, 2026-08-23. The internal `struct rt_scheduler`
landed in `src/scheduler.h`; this file holds the part that has no
implementation yet, so that no header in `src/` declares functions that cannot
be written.

## Why this is prose and not a header

Every header in `src/` documents code that exists. The `uc_scheduler_*`
surface needs `clone`, a second ring, and a scheduler registry — milestone 3
at the earliest — so a header carrying it would be a standing lie about what
the runtime does. When the public/private include split happens
(`include/uc/scheduler.h` against `src/scheduler.h`), this section is the spec
to write it from.

The split's acceptance test is mechanical: the public header includes
`<stdint.h>` and nothing else, project or kernel. `src/scheduler.h` includes
`ring.h` today, which pulls `<linux/io_uring.h>`, so a public header derived
from it by copying would drag the kernel's uapi into every consumer.

## The interface

```c
typedef uint64_t uc_scheduler_id;

constexpr uc_scheduler_id UC_SCHEDULER_INVALID = {};

/* Called by a thread, on itself. Returns UC_SCHEDULER_INVALID on failure. */
[[nodiscard]] uc_scheduler_id uc_scheduler_become(unsigned cpu);
[[noreturn]] void uc_scheduler_run(void);

[[nodiscard]] uc_scheduler_id uc_scheduler_current(void);
```

`{}` rather than `0` so the initializer survives the id becoming a struct
(generation plus slot) without touching call sites.

`[[nodiscard]]` throughout: `ring.h` states the convention, and a handle that
is the only route to a scheduler is the strongest case for it — especially
once the zero handle means failure.

## Rejected: a create call that spawns its own thread

The obvious shape is `uc_scheduler_create_on(cpu)` — the runtime clones a
thread, pins it, sets up its ring, and hands back a handle once the new
scheduler is ready. It was drafted that way and is wrong. Three reasons:

- **It invents a runtime that does not exist.** At `_start` there is no
  scheduler and no runtime object, just a thread. `libuc.md`'s boot contract
  is explicit that **the scheduler is never created; the boot thread becomes
  it** — its state is a struct the thread fills in for itself. A create call
  implies an ambient owner with the standing to manufacture threads, and
  nothing in the design is that.
- **It makes one object with two origins.** Scheduler 0 would be a thread that
  became a scheduler; scheduler N would be a thread some other scheduler
  produced. Same struct, same loop, two incompatible stories about how it came
  to exist — and the boot path, which is the one that has to work before
  anything else does, would be the special case.
- **It hides a rendezvous.** "Returns once the new scheduler is pinned and its
  ring is ready" is a handshake between two threads, in a design whose whole
  premise is that threads do not share state. Become-on-self needs no
  handshake at all: the thread that needs the state is the thread that writes
  it, and `SINGLE_ISSUER` wants exactly that thread to own the ring anyway.

So the sequence is identical for every scheduler, boot included: a thread
exists, it calls become on itself, it enters the loop. `rt_scheduler_init`
already has this shape.

The cost, stated plainly: **the program creates the thread.** Placing a thread
on a CPU stays program text, which is the right place for it in a
thread-per-core design — `libuc.md` already spends `pthread_create` on fibers
for the calling scheduler, so scheduler creation was never going to be that
call regardless.

## Rejected: an attribute struct, for now

`struct uc_scheduler_attr` with placement, cpu, and ring entries was drafted
and dropped. Two reasons:

- **`ring_entries` is a layering violation.** SQ depth is io_uring mechanics;
  a program has no business naming it. If sizing ever needs to come from the
  caller it comes back as a workload statement — an expected fiber or
  connection count — that the runtime translates into ring geometry.
- **An extensible struct needs versioning it cannot yet justify.** libuc has
  no external consumers, so ABI stability is a cost against no benefit.

The migration path matters more than the starting point: when attributes are
needed, add a `_with` variant taking the struct *alongside* the existing
calls. Adding a function is ABI-safe; growing a struct is the hazard. Make
that struct size-first when it appears — `io_uring_params`, `clone_args` and
`sched_attr` all solve extension with a size or a reserved field, and this
runtime reads those headers already.

## Undefined, deliberately

Named so the interface above is not read as complete.

- **Nothing consumes an id.** `uc_scheduler_current()` returns a value with no
  consumer until `uc_spawn_on(id, fn, arg)` exists, and that function is what
  forces the registry: an id-to-pointer table read by every scheduler.
  Invariant 3 permits it only as explicitly shared state, and publishing an
  entry while other schedulers read the table is the synchronisation question
  that has not been answered.
- **No destroy, so no generations.** A generation-bearing id is only earned by
  slot reuse, which needs a lifecycle: what happens to a destroyed
  scheduler's fibers, its ring, and its in-flight operations. Until that
  exists the id carries no generation, and `struct rt_scheduler` carries no
  id field at all — a field written once and read by nothing reads as state
  the runtime maintains.
- **Nothing prevents a fiber from being on two queues.** `ready_next` is a
  single field, so a fiber linked into two lists corrupts both. `wake_one`
  removed the only sanctioned way to do it, but `rt_fiber_queue_push` is still
  reachable from fiber context for program-owned queues, and nothing checks.
  Detecting it needs a fiber to record which queue holds it, which is a field
  whose only reader would be a panic — deferred on the same grounds as the
  state enum.
- **The deadlock report says nothing about who.** The condition is exact —
  ready empty, nothing staged, nothing in flight, fibers alive — and verified
  reachable. But `libuc.md`'s spike found the entire value was in naming the
  fiber and what it blocked on, and that needs a registry of live fibers,
  which does not exist. Today it is a correct panic, not a diagnosis.
- **No shutdown or join.** `rt_scheduler_run` returns today when its last fiber
  dies, which is a probe convenience — `main.c` runs two phases on one
  scheduler. The destination is a loop that does not return merely because
  the ready queue emptied: an idle scheduler waits for local I/O or a
  cross-scheduler message. That distinction only becomes real when a
  scheduler can receive one.
- **Failure detail has nowhere to go.** `become` returns
  `UC_SCHEDULER_INVALID` and the reason belongs in errno, which AGENTS.md
  forbids and which does not exist. The internal `rt_scheduler_init` returns
  raw `-errno` like everything else in the runtime; the public call joins the
  queue behind per-fiber errno, a deliverable `libuc.md` already schedules.
  Shaping the public call around today's constraint would buy nothing, since
  none of this interface is implementable at the current milestone anyway.
- **Nothing is pinned.** `struct rt_scheduler` has no cpu field either, and
  `sched_setaffinity` is never called; there is one thread. Placement returns
  as a parameter to `rt_scheduler_init`, which is where the become model puts
  it — a thread pins *itself* at the moment it becomes a scheduler, in the
  same call that binds its ring to it under `SINGLE_ISSUER`.
- **Placement is a single cpu, not a mask.** `sched_setaffinity` takes a set.
  A program that wants "any of these four" cannot say so. Kept narrow
  deliberately; noted so the narrowness is a choice.

## The request protocol

A fiber does not call its scheduler; it leaves a message and gives up the CPU.
`struct rt_fiber_request` is that message — a kind plus, where the kind needs
one, a payload:

```
NONE   cleared by the scheduler before every entry; seeing it on return
       means control was transferred without going through fiber.c
YIELD  still runnable
IO     carries a struct io_uring_sqe the scheduler will stage
EXIT   the fiber function returned
PARK   carries a wait queue; only another fiber can undo it
WAKE   carries a wait queue to take one fiber from — serviced without
       suspending
SPAWN  carries an entry point and argument — serviced without suspending
```

### The request protocol is an algebraic-effect handler

This is not merely an analogy to effects; it is the operational shape of one.
The runtime does not expose effect syntax, typed effect rows or programmable
handlers, but all of the machine parts are present:

| effect term | runtime representation |
|---|---|
| effect signature | `enum rt_fiber_request_kind` |
| operation arguments | `request.value` |
| perform | write the request, then `rt_fiber_suspend` |
| continuation | the fiber's saved context and stack |
| handler | `rt_scheduler_resume` plus dispatch |
| resumption policy | the ready, submit and wait queues |
| operation result | `fiber->result` |

The continuation is concrete rather than compiler-manufactured: it is the rest
of the fiber's computation, delimited by the fiber entry/exit boundary and
preserved on its stack. It is one-shot. The scheduler may retain it, move it
between queues, resume it, or discard it at EXIT, but it cannot clone it and a
fiber cannot resume itself. Queue membership is therefore ownership of a
continuation, not merely bookkeeping about one.

The current effect signature is:

```text
Yield : () -> ()
Io    : Sqe -> i32
Park  : WaitQueue -> ()
Wake  : WaitQueue -> Bool
Spawn : (fn, arg) -> Result
Exit  : () -> Never
```

Their handlers choose different resumption policies. YIELD places the
continuation at the back of the ready queue. IO retains it until the CQE.
PARK transfers it to a wait queue. WAKE and SPAWN mutate scheduler-owned state
and immediately resume the performing continuation. EXIT never resumes it and
returns its fiber to the idle queue.

IO is a nested effect boundary rather than a second implementation of kernel
I/O:

```text
fiber performs IO { SQE }
    -> scheduler retains the continuation and stages the SQE
    -> kernel completes with a CQE
    -> scheduler writes fiber->result and resumes the continuation
```

That is what lets ordinary blocking C remain direct style. `write(fd, buf,
len)` looks like a call, while underneath it performs IO and the scheduler
handles it. The buffer loan travels with the retained continuation: the handler
may not resume or discard that continuation as though the operation were over
until the kernel has released every referenced address. The cancellation rule
below is the resource consequence of the effects model, not an io_uring
special case.

The admission rule for future request kinds is therefore:

> Put an operation in this protocol when it must transfer the continuation or
> requires authority over scheduler-owned state. Otherwise it remains an
> ordinary call.

This keeps the request union an effect signature rather than a miscellaneous
command bucket. It admits YIELD, IO, PARK, WAKE, SPAWN and EXIT. It does not by
itself admit `malloc`; that depends on who ultimately owns allocation state,
which is the open question below.

**WAKE is a request, not a call, and that is the point.** A fiber could
enqueue onto the scheduler's ready queue directly; it would work, because the
runtime is cooperative and single-threaded and the push happens while the
scheduler is parked inside `rt_switch`. It would also be scheduler state
mutated from fiber context under three unchecked conditions, and the first
`rt_fiber_queue_pop` from fiber context — waking a fiber that is already
queued, say — would corrupt `ready_next` by linking it into two lists.

**WAKE names the wait queue, never a fiber.** It first took a
`struct rt_fiber *`, which meant the caller wrote
`rt_fiber_queue_pop(&q)` and then `rt_fiber_wake(f)` — two steps with the
fiber on no queue at all in between, owned by nothing but a local variable.
Every failure mode lived in that window: skip the second step and the fiber is
unreachable while still counted live, so the scheduler eventually reports a
deadlock it cannot attribute; repeat it and the same fiber is pushed twice,
which is the `ready_next` corruption above arriving by the sanctioned route
rather than the forbidden one.

The fiber pool made a third one possible that had not existed before. While
fibers were never recycled a retained `struct rt_fiber *` stayed valid
forever; once `rt_fiber_start` re-initialises a fiber taken from `idle`, a
stale pointer names a later incarnation, and waking it is an ABA that corrupts
whichever queue the new occupant is on.

`rt_fiber_wake_one(struct rt_fiber_queue *)` closes all three by construction,
because there is no window and no pointer: the scheduler pops from the wait
queue and pushes onto ready inside one serviced request, and returns whether
it found anyone. The pair is symmetric — `rt_fiber_park` and
`rt_fiber_wake_one` both name only the queue, and neither side of a handoff
can hold a fiber across it.

That is the transferable part of the Rust operation model. C cannot prove the
transfer, but it can refuse to hand out the pointer that makes getting it
wrong expressible: **wait queue owns the fiber → the scheduler moves it →
ready queue owns the fiber**, with no representable state in between. Waking a
*specific* fiber is deliberately not offered; when something needs it, it wants
a generation-bearing handle rather than a raw pointer, which is the same answer
the id encoding above already reaches for.

So `rt_scheduler_resume` loops: it services a WAKE and switches straight back,
returning only on a request that genuinely suspends. The waker keeps the CPU
and is never re-queued. It costs a switch round trip, roughly 12 ns, and buys
the property that every mutation of scheduler state happens in `scheduler.c`
with the scheduler actually running.

That splits requests into two categories worth naming: **suspending** (YIELD,
IO, PARK, EXIT), which dispatch files onto a queue, and **serviced**
(WAKE, SPAWN), which never reach dispatch — either arriving there means the
resume loop let one through, and it panics.

The one case that does not fit this shape is `malloc`. A switch round trip
roughly doubles an allocation, and `libuc.md` says the per-scheduler arena is
what forces the whole design. That is a signal about the arena being
questionable — two schedulers can share a CPU, so per-scheduler allocation was
never obviously right — rather than a reason to let fibers touch scheduler
state. Unanswered until there is an allocator.

This is the only thing a fiber writes for the scheduler to read. There is no
companion state field: what a fiber *is* follows from what the scheduler did
with the request, and the scheduler does not need to write down its own
conclusions.

**Why a tagged union rather than a state value.** Encoding the request in a
fiber state works only while every request corresponds to a distinct resulting
state and needs no arguments. That stopped being true when IO needed an SQE;
PARK and WAKE share a wait-queue payload and SPAWN carries a third shape.
An enum value can name the operation but cannot carry its arguments, which is
exactly the effect-signature distinction between an operation and its
invocation.

**The dividend was single ownership of the fiber state, and then the state
itself.** Before the split, the fiber wrote one state on its way out and the
scheduler overwrote it with another: one field, two writers, held together by
a comment. Making the request explicit gave the field a single writer — and
then left it with nothing to say. Every value it carried is derivable from
something already load-bearing:

| | derivable from |
|---|---|
| running | the fiber `rt_scheduler_resume` just entered |
| ready | on the ready queue |
| pending submission | on the submit queue |
| blocked | `owner != nullptr` |
| dead | on no queue, not in `live_count` |

It survived one change as a write-only field — seven assignments, and a single
read that was strictly redundant with the `owner` check running immediately
before it — and was deleted. The distinction it drew between "queued behind a
full SQ" and "the kernel owes a completion" is real and still drawn; it is
queue membership against `owner`, which are the facts the scheduler acts on
anyway rather than a shadow written alongside them.

The one thing that had to improve to lose it: the reap loop's single check
became two, because a null `owner` and a foreign `owner` are different
failures — a stale or duplicate completion against a fiber that moved with
work in flight — and one branch reporting "owned elsewhere" was misleading
whenever `owner` was null.

**No `default` in the dispatch switch.** With one, a kind added later would
fall silently into the "fiber suspended without a request" panic. Without one,
`-Wswitch -Werror` makes adding a kind a build failure until every dispatch
site handles it. Verified twice: adding `RT_REQUEST_PARK`, and later
`RT_REQUEST_SPAWN`, each failed the build at the dispatch switch until handled.
Under a policy with no regression net, a compile error is worth more than a
runtime one.

**The local check forces this choice rather than expressing it.** `make check`
runs `-Weverything`, which contains both `-Wswitch-default` (demands a default
label) and `-Wcovered-switch-default` (rejects a default that covers every
enumeration value). A fully-covered enum switch satisfies neither together, so
one has to be suppressed, and which one is the whole decision. The Makefile
suppresses `switch-default` and keeps `covered-switch-default`, which is the
check that actively enforces the style above.

Worth knowing if the flags are ever revisited: a `default` label does **not**
have to cost exhaustiveness, because `-Weverything` also carries
`-Wswitch-enum`, which fires on a missing enumerator even when a default is
present. Measured, not assumed. That path was rejected anyway — the container
build runs `-Wall -Wextra`, which has `-Wswitch` but not `-Wswitch-enum`, so a
default would hold the guarantee locally and quietly drop it in the build that
ships. Keeping no default holds it in both under flags they already have.

**Dispatch is not part of resume.** Entering a fiber and filing it afterwards
are separable; queue policy belongs to the loop, and `main.c`'s RT-004 driver
resumes a fiber by hand and must not acquire queue side effects. That driver
loops on `request.kind != RT_REQUEST_EXIT` rather than on state, because
standing in for the loop means reading what the fiber said, not what a
scheduler would have concluded.

## Buffer lifetime, and what will break it

Every address a fiber puts in a request must stay valid until `rt_fiber_await_io`
returns. C cannot type that, and the guarantee does not come from the pointer
— it comes from the protocol: **the fiber cannot run again until the reap loop
has seen its CQE**, so it cannot return past the frame holding the buffer, and
cannot reuse or overwrite it.

**Validity is necessary and not sufficient; the requirement is exclusive
access.** For the whole window the memory must not be read or written by
anyone — not just not freed. The kernel may write a receive buffer at any
point until the CQE, so its contents are indeterminate throughout, and a read
of even the untouched tail is a read of an in-flight destination.

The protocol argument covers the suspending fiber and **stops there**, which
is the hole worth naming: it says nothing about any *other* fiber, and those
are running. It holds today only because of where addresses come from —
a fiber's own stack, which no other fiber can name, or static storage that
happens not to be shared. The second half of that is a convention, not a
mechanism: two fibers sharing one static buffer is ordinary C, compiles
silently, and breaks this immediately. It is the same aliasing gap that makes
the Rust model untranslatable, arriving from the other direction.

Stated at `rt_fiber_await_io` in `fiber.c`, which is the only place a
pointer-bearing request is published, and the reason all nine operations route
through one function rather than each calling suspend for itself.

**The window is wider than "the kernel has it."** A request sits in
the scheduler's submit queue — waiting for SQ space, `owner` still null,
nothing told to the kernel — before it is staged. The buffer must survive the
whole
call, not just the kernel's part of it.

Two things break the argument, and neither announces itself:

- **Cancellation.** `IORING_OP_ASYNC_CANCEL` does not withdraw a request; it
  makes it complete with `-ECANCELED` (`cancel.c:476` queues the original to
  fail). So the sequence is always **cancel → reap the original CQE →
  reclaim**, never cancel and free. Freeing on the cancel *request* is a
  use-after-free with the kernel holding the pointer.
- **An allocator.** Today every address is on the suspending fiber's own stack
  or in static storage, because nothing else exists to point at. Once there is
  a heap, a buffer can be freed by a fiber that *can* run, and "this one
  cannot" stops covering it. This is the closer of the two.

**A dividend from separating "queued to submit" from "staged"**, which was
done for backpressure reasons: it is also the cancellation boundary.
Cancelling a request still on the submit queue is free — dequeue it, mark the
fiber ready, no CQE ever exists, because the kernel was never told. Only a
staged request needs the
cancel-reap-reclaim dance. `owner != nullptr` is exactly the test for which
regime applies.

## The completion key, later

`sqe->user_data = fiber` works because one fiber has at most one operation
outstanding. Three things end that, and all three want the same answer:

- cancellation, where a CQE can arrive for an operation the fiber has stopped
  waiting on;
- multishot, where one request produces many CQEs;
- several concurrent operations per fiber.

At that point the completion key must identify an **operation record**, not a
fiber — scheduler-owned, with a generation, living until the final CQE for that
operation. `fiber.h`'s request field notes the same thing from the fiber side: a
bigger fiber struct is the wrong answer, scheduler-owned records are the right
one.

The lifetime rule that falls out: **a fiber's stack may be reclaimed only when
it has zero operations in flight**, which is `owner == nullptr` today and a
per-fiber in-flight count once one fiber can have several.

Not built. `libuc.md` already sketches generation-plus-slot encoding; what
matters now is that nothing in the current design assumes the key will stay a
bare fiber pointer.

## Rejected: an Operation abstraction in C

The Rust port (`/tmp/rt-rs`) sketches a private `Operation` trait —
`prepare(&mut self, sqe)` and `complete(self, result)` — where the operation
struct holds the buffer borrow, so the lifetime is proven by the type system.

That does not translate. Recreating it in C with callbacks and `void *` state
adds machinery and buys no lifetime proof, because C has no aliasing
guarantee: `const` restricts one access path, not every alias to the same
memory. The C version is an auditable protocol, never a safe API, and
pretending otherwise with structure would obscure that.

Two parts of it were worth taking, and one was already true:

- **Already true.** The trait's "no raw SQE pointer escapes" goal is satisfied
  structurally by the request descriptor — `rt_ring_sqe` has one runtime call
  site, in the scheduler's staging loop. There is no SQ borrow left to
  constrain.
- **Taken.** One executor rather than per-operation staging: `rt_fiber_await_io`.
- **Deferred.** A `complete` step. In C it collapses to translation, but it is
  the seam where two things will land: the libc `res < 0 → errno = -res;
  return -1` boundary, and `read`/`recv` into uninitialized memory, where only
  the first `cqe.res` bytes may be treated as written.

## Migration

`fiber->owner` is not "the scheduler this fiber belongs to" — it is **the ring
that owes this fiber a completion, or null.** Set when the scheduler stages the
fiber's request into an SQE, cleared when the CQE is reaped, read only by the
scheduler.

`owner == nullptr` is therefore **necessary for migration eligibility and not
sufficient** — a correction to the first statement of this, which claimed the
field was the whole rule. A fiber that owes no completion may still be linked
into a scheduler's ready or submit queue, and one parked on a wait queue is
waiting on a condition that scheduler's own fibers signal. Migration is a
dequeue and a handoff at an explicit point; the field tells you only that the
kernel is not holding a pointer into the fiber.

That is a property of the descriptor design, not a promise bolted onto it. A
fiber describes an operation into its own `req` and suspends; it never touches
a ring and never names a scheduler, so a *ready* fiber is not bound to anything
in the first place. `suspend_to` is rewritten on every entry, so a fiber resumed
by a different scheduler switches back to that one with nothing to correct.

What actually gates migration, none of it in this struct:

1. **Completion routing.** A CQE arrives at the ring that issued it. Handled
   structurally rather than left open: a fiber that owes a completion has a
   non-null owner and is therefore not eligible to move. Forwarding would only
   be needed to make migration legal *during* an in-flight operation, which
   nothing wants.
2. **The per-core arena.** `libuc.md` is explicit that the allocator forces
   the design: allocate on one, free on another, and invariant 3 breaks on
   the first `free`. This is the wall, and it stands even for two schedulers
   sharing a CPU, since each owns its own allocator.
3. **Provided buffer rings.** `transport.md`'s core-owned receive buffers — a
   migrated fiber holding one must return it to the scheduler it came from.
4. **Per-fiber TLS.** Fine if the fiber carries its own TCB; broken the moment
   anything per-*scheduler* is cached into a fiber's TLS image.
