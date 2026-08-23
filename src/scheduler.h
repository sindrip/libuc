/*
 * A cooperative fiber scheduler: fibers suspended on ring completions.
 *
 * A scheduler is the ownership boundary, not a synonym for a CPU. It owns its
 * ring, its fibers, its ready queue, and eventually its fiber slab, stack pool
 * and allocator. It is pinned to one CPU while it runs, but more than one
 * scheduler may be pinned to the same CPU.
 *
 * Every field below is mutated only by the scheduler's own thread and is
 * deliberately non-atomic (invariant 3). State another scheduler can reach
 * belongs in an explicitly shared endpoint type, never here — and that holds
 * even when two schedulers share a CPU, because two runnable threads on one
 * core are preempted against each other by the kernel. Invariant 7 is about
 * fibers within a scheduler; it says nothing about the threads underneath.
 *
 * The dispatch mechanism is the whole design: a ring op stamps
 * user_data = the fiber pointer, so the fiber IS the completion key — the reap
 * loop casts it back, delivers cqe->res into fiber->result, and marks the fiber
 * ready. No completion table, no lookup. Non-fiber completions, when they
 * appear, will claim tag bits in user_data; noted, not built.
 *
 * The public uc_scheduler_* surface — creation, placement, identity, remote
 * spawn — needs clone, a second ring and a scheduler registry, none of which
 * exist. It is specified in .scratch/scheduler.md and deliberately absent
 * from src/: every header here documents code that exists.
 */
#ifndef RT_SCHEDULER_H
#define RT_SCHEDULER_H

#include <stddef.h>

#include "ring.h"
#include "fiber.h"

struct rt_scheduler {
  /* The scheduler's own register state: where a suspending fiber switches
   * back to. Never primed like a fiber's — it is captured lazily by the first
   * switch away from it, which is why a scheduler is not created the way a
   * fiber is. The boot thread simply becomes one. */
  struct rt_ctx context;

  struct rt_ring ring;

  /* Two queues, because a fiber suspends for two different reasons. ready is
   * what the loop drains; submit holds fibers that filled in a request and are
   * waiting for it to reach an SQE. A fiber is on at most one, which is why
   * they share rt_fiber.ready_next.
   *
   * submit is what makes a full SQ a non-event: staging drains as far as ring
   * capacity allows and leaves the rest for next turn, so no fiber ever sees
   * backpressure from a resource only the scheduler can free. */
  struct rt_fiber_queue ready;
  struct rt_fiber_queue submit;

  /* The loop's whole state machine, and the reason blocked-ness is not
   * counted: at the wait point nothing is running, so blocked is
   * live_count - ready.count and would be a second thing to keep in sync.
   *
   *   ready > 0                       run; still enter, without waiting
   *   ready == 0, inflight > 0        submit and wait for one completion
   *   ready == 0, inflight == 0, live > 0   nothing can ever run again
   *   live == 0                       every fiber is dead; return
   *
   * inflight is the count that is not derivable and the one that separates
   * an idle server from a genuine deadlock: a fiber parked on RECV with no
   * traffic waits forever and is not wedged. It counts operations staged or
   * submitted whose CQE has not been reaped — staging increments before the
   * suspend, so it is always at least the unpublished SQE count.
   *
   * The intra-scheduler waiter count that a real deadlock detector wants
   * (libuc.md) cannot be added until there are primitives to wait on, so row
   * three is a panic rather than a diagnosis. */
  size_t live_count;
  size_t inflight_count;

  /* The scheduler-owned fiber slab, stack pool and allocator join this
   * structure when those types land. Their backing mappings may grow, but
   * neither this object nor a live fiber moves. */
};

/* There is no rt_self. Nothing outside this file needs to find a scheduler:
 * the loop and resume take theirs as a parameter, and fiber-side code reaches
 * everything it needs through rt_current and the fields the scheduler pushed
 * into the fiber before entering it (fiber.h). The only ambient state in the
 * runtime is which fiber is running, which is per-fiber rather than per-core
 * and so needs no correction when a fiber moves. */

/* Fill in a scheduler. Returns 0, or -errno
 * straight from rt_ring_setup — same conventions as ring.h.
 *
 * Called on the thread that is becoming the scheduler, never on its behalf:
 * the ring is bound to its issuer by SINGLE_ISSUER. Boot is not a special
 * case — every scheduler is a thread that ran this on itself, which is why
 * nothing here creates one. */
[[nodiscard]] int rt_scheduler_init(struct rt_scheduler *s,
                                    unsigned ring_entries);

/* Create a fiber and put it on the ready queue. The scheduler is passed rather
 * explicitly because a scheduler may be populated before it runs — boot fills
 * in scheduler 0 and spawns into it while still being the only thread, and
 * there is no ambient scheduler to take it from in any case. */
void rt_scheduler_spawn(struct rt_scheduler *s, struct rt_fiber *t,
                        void (*fn)(void *), void *arg);

/* The loop. Runs a turn's worth of ready fibers, publishes the SQEs they
 * staged, waits only when nothing is left to run, and reaps completions back
 * into their fibers. Returns when the last fiber dies.
 *
 * Returning at all is a probe convenience — main.c runs two phases on one
 * scheduler. The destination loop does not return merely because the ready
 * queue emptied; an idle scheduler waits for local I/O or a cross-scheduler
 * message, which is only real once a scheduler can receive one
 * (.scratch/scheduler.md).
 *
 * Submission is batched once per turn — not an optimization: SQ_REWIND and
 * the in-kernel BPF loop both assume the loop owns submission timing, and
 * batching is what cached_sq_tail exists for. The turn is a snapshot of the
 * ready count taken before draining it, so a fiber that only ever yields is
 * re-queued behind the snapshot and cannot starve submission by looping. */
void rt_scheduler_run(struct rt_scheduler *s);

/* Enter one ready fiber and regain control when it yields, blocks or exits.
 *
 * Writes the two things a fiber needs before it can suspend: rt_current, and
 * the fiber's suspend_to. Rewriting suspend_to on every entry rather than once at
 * spawn is what keeps it honest — a fiber entered by a different scheduler
 * switches back to that one, with nothing to correct.
 *
 * A fiber being resumed must owe no completion. t->owner is checked, not
 * assumed: it is nullptr for anything the ready queue hands out, so a non-null
 * one means a fiber became runnable while an operation was still in flight —
 * the exact corruption that would otherwise surface as a CQE arriving for a
 * fiber that has already moved on. */
void rt_scheduler_resume(struct rt_scheduler *s, struct rt_fiber *t);

#endif /* RT_SCHEDULER_H */
