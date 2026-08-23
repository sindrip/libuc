/*
 * Scheduler bodies: the loop, the ready queue, the switch into a fiber, and
 * the ring ops that fibers suspend on. Contracts are in scheduler.h.
 */

#include "scheduler.h"

#include <stdatomic.h>
#include <stdint.h>

#include <asm/errno.h> /* EAGAIN */

#include "crash.h"
#include "fiber.h"
#include "syscall.h"

int rt_scheduler_init(struct rt_scheduler *s, unsigned ring_entries) {
  /* The caller's struct is an uninitialized local; the queue and the counters
   * are only correct from a known-zero start. */
  *s = (struct rt_scheduler){};

  auto ret = rt_ring_setup(&s->ring, ring_entries);
  if (sys_failed(ret)) {
    return ret;
  }

  return 0;
}

void rt_scheduler_spawn(struct rt_scheduler *s, struct rt_fiber *t,
                        void (*fn)(void *), void *arg) {
  rt_fiber_create(t, fn, arg);
  s->live_count++;
  rt_fiber_queue_push(&s->ready, t);
}

void rt_scheduler_resume(struct rt_scheduler *s, struct rt_fiber *t) {
  if (t->owner != nullptr) {
    rt_panic("scheduler: resume of a fiber awaiting a completion",
             __builtin_return_address(0));
  }

  t->suspend_to = &s->context;

  /* Cleared, not merely overwritten later: a fiber that gives up the CPU
   * without saying why must be caught, and it can only be caught if what is
   * in the slot afterwards cannot be last time's answer. */
  t->request.kind = RT_REQUEST_NONE;

  rt_fiber_set_current(t);

  /* Runs the fiber until it makes a request that actually suspends it. WAKE
   * does not: it is serviced here and control goes straight back, so a fiber
   * that wakes a peer keeps the CPU. Doing it in this loop rather than in
   * dispatch is what makes that true — dispatch runs only once the fiber has
   * given up the CPU for real.
   *
   * The kind is cleared before each re-entry for the same reason it is cleared
   * before the first: a fiber that suspends without saying why must not be
   * filed under the request it made last time. */
  for (;;) {
    rt_switch(&s->context, &t->ctx);

    if (t->request.kind != RT_REQUEST_WAKE) {
      break;
    }
    rt_fiber_queue_push(&s->ready, t->request.value.wake);
    t->request.kind = RT_REQUEST_NONE;
  }

  /* Nulled the instant control is back: from here to the next resume there is
   * no current fiber, so an operation issued from scheduler context faults on
   * the spot instead of acting on the fiber that just left. */
  rt_fiber_set_current(nullptr);
}

/* File a returned fiber according to what it asked for. Deliberately not part
 * of rt_scheduler_resume: entering a fiber and deciding where it goes
 * afterwards are separable, queue policy belongs to the loop, and main.c's
 * RT-004 driver resumes a fiber by hand and must not get queue side effects.
 *
 * No `default` label. Under -Wswitch -Werror a request kind added later fails
 * the build here until it is handled, rather than being silently absorbed by
 * the panic below. */
static void rt_scheduler_dispatch(struct rt_scheduler *s, struct rt_fiber *t) {
  switch (t->request.kind) {
    case RT_REQUEST_YIELD:
      rt_fiber_queue_push(&s->ready, t);
      break;

    case RT_REQUEST_IO:
      rt_fiber_queue_push(&s->submit, t);
      break;

    case RT_REQUEST_PARK:
      /* Onto a queue this scheduler does not own and never inspects. Only a
       * WAKE naming this fiber will move it again — no kernel event exists
       * that could. */
      rt_fiber_queue_push(t->request.value.wait_queue, t);
      break;

    case RT_REQUEST_WAKE:
      /* Serviced in rt_scheduler_resume, which returns only on a request that
       * suspends. Reaching dispatch means that loop let one through. */
      rt_panic("scheduler: wake reached dispatch",
               __builtin_return_address(0));

    case RT_REQUEST_EXIT:
      s->live_count--;
      break;

    case RT_REQUEST_NONE:
      /* Reachable only through the switch itself: every path out of a fiber
       * body sets a request first, so an empty slot means something
       * transferred control without going through fiber.c. */
      rt_panic("scheduler: fiber suspended without a request",
               __builtin_return_address(0));
  }
}

void rt_scheduler_run(struct rt_scheduler *s) {
  for (;;) {
    /* A turn is the fibers that were ready when it began, snapshotted before
     * draining. Without the snapshot a fiber that only yields is popped and
     * re-queued forever and the loop never reaches submission, so the
     * batch-once-per-turn contract would hold only for fibers that block. */
    for (size_t n = s->ready.count; n > 0; n--) {
      struct rt_fiber *t = rt_fiber_queue_pop(&s->ready);

      rt_scheduler_resume(s, t);
      rt_scheduler_dispatch(s, t);
    }

    /* Stage what the turn requested, as far as ring capacity allows. What
     * does not fit stays on the submit queue and goes next turn — the reason
     * a fiber can always issue an operation.
     *
     * user_data is stamped here, not by the fiber: completion identity is the
     * scheduler's encoding to choose. owner is set in the same breath, and the
     * two say the same thing — this ring now owes this fiber a completion. */
    while (s->submit.count > 0) {
      struct io_uring_sqe *sqe = rt_ring_sqe(&s->ring);
      if (sqe == nullptr) {
        break;
      }

      struct rt_fiber *t = rt_fiber_queue_pop(&s->submit);
      *sqe = t->request.value.io;
      sqe->user_data = (unsigned long)(uintptr_t)t;

      t->owner = s;
      s->inflight_count++;
    }

    if (s->live_count == 0) {
      return; /* rt_main falls into its idle loop */
    }

    /* The staged count is the private cursor's lead over the published tail:
     * every op staged this turn goes to the kernel in one enter. */
    auto staged = s->ring.cached_sq_tail -
                  atomic_load_explicit(s->ring.sq_tail, memory_order_relaxed);

    /* Wait only with nothing left to run and nothing left to stage. Waiting
     * while a fiber is ready would park the whole scheduler on the kernel while
     * it has work in hand; waiting with requests still queued would delay them
     * behind a completion for no reason, since the SQ frees as the kernel
     * consumes this batch. */
    bool wait = s->ready.count == 0 && s->submit.count == 0;

    /* Nothing runnable, nothing to stage, and nothing the kernel owes us: every
     * live fiber is parked on a queue, and only a fiber could signal it. This
     * is deadlock, and it is exact rather than heuristic — the scheduler holds
     * the whole state, so "no fiber can ever run again" is a local fact.
     *
     * It cannot fire for an idle server. A fiber blocked on RECV with no
     * traffic is counted in inflight, and inflight being nonzero is precisely
     * the difference between waiting and being wedged.
     *
     * What it does not yet do is say *which* fibers, and on which queues.
     * libuc.md's spike found the value was entirely in that: it named the
     * fiber and what it blocked on, which is what turned a hang into a
     * diagnosis. Naming them needs a registry of live fibers, which does not
     * exist. */
    if (wait && s->inflight_count == 0) {
      rt_panic("scheduler: deadlock — every live fiber is parked",
               __builtin_return_address(0));
    }

    /* Enter whenever anything is outstanding, waiting or not. Not an
     * optimization and not only about submitting: under DEFER_TASKRUN a
     * completion is deferred work that reaches the shared CQ *only* inside
     * io_uring_enter, where io_cqring_wait runs it (wait.c:189-198). A loop
     * that entered only when it had SQEs to publish or intended to block would
     * never reap while some other fiber stayed runnable — one fiber looping on
     * rt_fiber_yield would starve another's completed RECV forever, with the
     * CQ permanently empty because nothing ever asked the kernel to fill it.
     *
     * inflight_count alone is the right test because it subsumes the other two
     * reasons to enter: staging increments it, so staged > 0 implies it is
     * nonzero, and the deadlock check above has already ruled out waiting with
     * nothing in flight. */
    if (s->inflight_count > 0) {
      /* Success means the whole batch, not merely a nonnegative return. The
       * kernel may stop after a request-allocation failure or a bad SQE and
       * return a positive short count (io_uring.c:2046-2068); io_uring_enter
       * then skips the wait (io_uring.c:2646-2650). The shared tail already
       * exposes the entire batch, so carrying on would make the next staged
       * calculation zero while the unconsumed suffix remains behind sq_head,
       * stranding those fibers awaiting completions that never arrive.
       *
       * Retrying needs submission accounting based on cached_sq_tail -
       * sq_head. This milestone does not implement that state machine, so
       * fail loudly. */
      auto ret = rt_ring_submit_and_wait(&s->ring, staged, wait ? 1u : 0u);
      if (sys_failed(ret)) {
        rt_panic("scheduler: enter failed", __builtin_return_address(0));
      }
      if ((unsigned)ret != staged) {
        rt_panic("scheduler: short submit", __builtin_return_address(0));
      }
    }

    /* Reap: the CQE's user_data is the fiber — cast it back, deliver the
     * result, queue it. */
    struct io_uring_cqe cqe;
    while (rt_ring_reap(&s->ring, &cqe)) {
      struct rt_fiber *t = (struct rt_fiber *)(uintptr_t)cqe.user_data;

      /* Two different failures, which one check cannot tell apart. A null
       * owner means nothing was awaiting this completion at all: a duplicate
       * or stale CQE, or a reap reading the wrong stride and finding a
       * plausible-looking pointer. This is also the one-shot contract's
       * tripwire — a fiber stages exactly one SQE and the scheduler clears
       * owner when the CQE lands, so a second CQE for the same fiber arrives
       * to a null owner rather than silently overwriting result. */
      if (t->owner == nullptr) {
        rt_panic("scheduler: cqe for a fiber awaiting nothing",
                 __builtin_return_address(0));
      }

      /* A non-null owner that is not us means the fiber moved while an
       * operation was in flight — impossible today, since nothing migrates,
       * which is exactly why it is worth catching rather than assuming. This
       * is where a forwarding path would hang if migration ever lands. */
      if (t->owner != s) {
        rt_panic("scheduler: cqe for a fiber owned by another scheduler",
                 __builtin_return_address(0));
      }

      t->result = cqe.res;
      t->owner = nullptr; /* the debt is paid; the fiber is unbound again */
      s->inflight_count--;
      rt_fiber_queue_push(&s->ready, t);
    }
  }
}
