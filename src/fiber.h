/*
 * A fiber: a stackful cooperative thread of control, and the protocol by
 * which it hands the CPU back.
 *
 * A fiber is a function with its own mmap'd stack; it runs until it suspends
 * and there is no preemption (invariant 7). Everything about being one lives
 * here — birth, identity, and the three ways to give up the CPU. What a fiber
 * can *ask for* while suspending is io.h; those are descriptions, and this is
 * the machinery they are described to.
 *
 * Suspension is a two-field protocol and the fields have different owners.
 * The scheduler writes suspend_to before entering a fiber, so the fiber knows
 * where control goes without knowing whose context it is. The fiber writes a
 * request before leaving, so the scheduler learns what it was asked for
 * without having to infer it. Neither side reaches into the other; both read
 * a fiber struct that sits between them.
 *
 * Nothing here names a scheduler except as an opaque pointer the scheduler
 * itself writes. A fiber needs three things to suspend — itself, somewhere to
 * put its request, and somewhere to return to — and all three are on its own
 * struct, put there by whoever entered it. That is what leaves a ready fiber
 * unbound to any ring, which is in turn what makes migrating one a decision
 * rather than a rewrite.
 */
#ifndef RT_FIBER_H
#define RT_FIBER_H

#include <stddef.h>

#include <linux/io_uring.h>

#include "context.h"

/* Reached only as a pointer from here, so the definition is not needed and
 * including scheduler.h would be a cycle: it needs struct rt_fiber by value
 * for its queues. */
struct rt_scheduler;

/* What a fiber asks its scheduler for. A tagged union rather than a state
 * value, because the kinds do not carry the same thing: YIELD and EXIT are
 * bare, IO carries an operation. Encoding this as a fiber state instead —
 * which is what the runtime did before this existed — works only while every
 * request corresponds to a distinct resulting state, and stops working at the
 * first request that needs a payload, since no enum value can carry a
 * pointer. PARK, waiting on a queue another fiber will signal, is that first
 * request. Making the request explicit is also what left the state enum with
 * nothing to say, and it was deleted.
 *
 * NONE is not a request. The scheduler clears the kind before entering a
 * fiber, so a fiber that gives up the CPU without saying why is caught rather
 * than filed under whatever it asked for last time.
 *
 * The switch over these kinds deliberately has no `default` label: with one,
 * a kind added later would silently fall into the panic instead of failing
 * the build. Under -Wswitch -Werror, adding a kind is a compile error until
 * every scheduler that dispatches on it has been taught the new case. That is
 * the closest thing to a test this addition gets (AGENTS.md). */
enum rt_fiber_request_kind : unsigned {
  RT_REQUEST_NONE = 0,
  RT_REQUEST_YIELD = 1,
  RT_REQUEST_IO = 2,
  RT_REQUEST_EXIT = 3
};

struct rt_fiber_request {
  enum rt_fiber_request_kind kind;

  union {
    /* The kernel's own type, not a parallel description of one: staging is a
     * struct copy, and there is no second layout to keep in step with the
     * uapi header (invariant 4).
     *
     * user_data is deliberately NOT set by the fiber. The scheduler stamps it
     * at staging time, because completion identity is its encoding to choose
     * — libuc.md's generation-plus-slot scheme is not something a fiber
     * should know the shape of.
     *
     * 64 bytes in every fiber, live only between describing an operation and
     * staging it. Against a 64 KB stack that is noise; against a slab of
     * millions of fibers it is not, and the alternative — a pool of records —
     * puts an allocation back on the wake path. Embedded until that bites.
     *
     * One slot, so one outstanding operation per fiber, which is exactly
     * today's one-shot model. Multishot or several concurrent operations per
     * fiber need scheduler-owned operation records instead, not a bigger fiber. */
    struct io_uring_sqe io;
  } value;
};

/* A zeroed rt_fiber is coherent, and rt_fiber_create starts from that zero
 * rather than assigning field by field: it owes no completion, sits on no
 * queue, carries no request, and its ctx.lr is 0 — so resuming one that was
 * never created faults at address zero, an unambiguous report rather than a
 * silent wrong branch. A field added later is then zero by default instead of
 * uninitialized.
 *
 * There is deliberately no state field. What a fiber *is* was a five-valued
 * enum until the request protocol made every value derivable from something
 * already load-bearing: running is the fiber resume just entered, ready and
 * pending-submission are queue membership, blocked is owner != nullptr, and
 * dead is neither queued nor counted. It survived as a write-only field for
 * one change and was removed. */
struct rt_fiber {
  struct rt_ctx ctx;
  void *stack_base; /* the mmap base, INCLUDING the guard page */
  size_t stack_len; /* total mapping length (guard + usable) */
  int result; /* the reaper's delivery: cqe->res, already in place when a
                 blocked fiber resumes */
  void (*fn)(void *);
  void *arg;

  /* The scheduler that owes this fiber a completion, or nullptr.
   *
   * Not "the scheduler this fiber belongs to" — the fiber never asks. Set when
   * the scheduler stages req into an SQE, cleared when the CQE is reaped, and
   * read only by the scheduler. A completion comes back to the ring that
   * issued it, so between those two moments the fiber is bound to that ring
   * and nothing else.
   *
   * Which makes the migration rule the field rather than a convention: a fiber
   * with a null owner can move, one with a non-null owner cannot. Nothing to
   * remember and nothing to enforce elsewhere. */
  struct rt_scheduler *owner;

  /* Intrusive queue link, owned by the scheduler, meaningful only while this
   * fiber is queued. One field for both the ready and submit queues: a fiber is
   * on at most one at a time, since it leaves a queue by being resumed and
   * rejoins one only when it comes back. Intrusive so that enqueueing needs no
   * allocation — the alternative is a node allocator on the path that runs
   * every time a completion wakes a fiber. */
  struct rt_fiber *ready_next;

  /* What this fiber asked for on its way out. Written by the fiber, read and
   * cleared by the scheduler. */
  struct rt_fiber_request request;

  /* Where a suspending fiber switches to, written by the scheduler on every
   * resume. Typed as a context and not as a scheduler on purpose: the fiber
   * hands it to rt_switch and cannot reach a scheduler field through it even
   * by accident, so "fibers do not know their scheduler" is enforced by the
   * type rather than by discipline. Rewritten on every entry, so a fiber
   * resumed by a different scheduler simply switches back to that one. */
  struct rt_ctx *suspend_to;
};


/* Allocate a guarded stack, prime the context so the first switch into this
 * fiber enters the trampoline.
 *
 * No scheduler argument, because a fresh fiber belongs to none: owner is set
 * when a request is staged, and suspend_to when a scheduler enters it.
 * Creation only — the fiber is not queued and no live count is touched.
 * rt_scheduler_spawn is the call that does both; this one exists for the
 * driver in main.c that resumes a fiber by hand, and for a future spawn that
 * has to place a fiber before publishing it. */
void rt_fiber_create(struct rt_fiber *f, void (*fn)(void *), void *arg);

/* The fiber running on this thread. The one piece of ambient state fiber-side
 * code needs, and deliberately the *fiber* rather than the scheduler: fiber
 * identity is per-fiber and travels with one that moves, where a scheduler
 * pointer would have to be corrected.
 *
 * Behind functions rather than exposed as a variable, because this is the one
 * place the whole runtime has to change to become multi-threaded. Today it is
 * a plain global; it becomes _Thread_local when clone() lands, and deriving it
 * from sp instead — the trick Linux used for current, two instructions and no
 * global at all — is a .scratch/stacks.md question about whether stacks become
 * a power-of-two slab. Every call site survives all three.
 *
 * Set only by rt_scheduler_resume, and nulled between resumes: an operation
 * issued from scheduler context then faults at once instead of quietly acting
 * on the fiber that just left. */
[[nodiscard]] struct rt_fiber *rt_fiber_current(void);
void rt_fiber_set_current(struct rt_fiber *f);

/* The three ways a fiber gives up the CPU. Each fills in a request and
 * suspends; they differ in the request and in nothing else.
 *
 * rt_fiber_yield is deliberately not IORING_OP_NOP. A yield is not a kernel
 * operation — nothing is asked of the kernel — so invariant 1 does not reach
 * it, and routing it through the ring would turn a register swap into a
 * syscall and make a runnable fiber indistinguishable from one waiting on
 * I/O, which is the distinction the scheduler's counters exist to draw.
 *
 * rt_fiber_await_io is the one place in the runtime where a pointer-bearing
 * request is published, which is why io.h's operations are descriptions
 * handed to it rather than nine separate suspension sites. Its
 * LIFETIME RULE, which C cannot type and nothing checks:
 *
 *   Everything the request references must stay valid until this function
 *   returns.
 *
 * What makes that sound is the protocol rather than the pointer — this fiber
 * cannot run again until the reap loop has seen its CQE (scheduler.c), so it
 * cannot return past the frame holding the buffer, and cannot reuse or
 * overwrite it. The window is wider than "the kernel has it": a request sits
 * on the scheduler's submit queue waiting for SQ space, before the kernel is
 * told anything at all.
 *
 * Two things will break that argument and neither will announce itself:
 *
 *   - Cancellation. IORING_OP_ASYNC_CANCEL does not withdraw a request, it
 *     makes it complete with -ECANCELED (cancel.c:476 queues the original to
 *     fail). So the sequence is always cancel, reap the original CQE, then
 *     reclaim — never cancel and free.
 *   - An allocator. Today every address is on the suspending fiber's own
 *     stack or in static storage, because nothing else exists to point at.
 *     Once there is a heap, a buffer can be freed by a fiber that *can* run,
 *     and "this one cannot" stops covering it. This is the closer of the two.
 *
 * By value, and taking the whole SQE rather than handing back a pointer to
 * fill: no mutable request pointer escapes, and whole-struct assignment means
 * a request cannot inherit a buffer pointer, offset or flags from the last
 * operation this fiber issued. It costs nothing — clang builds the literal
 * directly into the fiber and tail-calls the suspend (verified at -O1).
 */
void rt_fiber_yield(void);
[[nodiscard]] int rt_fiber_await_io(struct io_uring_sqe sqe);

/* The trampoline branches here when a fiber function returns (switch.c).
 * Requests EXIT and suspends for the last time. */
[[noreturn]] void rt_fiber_exit(void);

#endif /* RT_FIBER_H */
