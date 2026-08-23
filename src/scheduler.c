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

/* FIFO. Push at the tail so a yielding fiber goes behind everything already
 * waiting — round-robin falls out, and a fiber that only yields cannot hold
 * the CPU against its peers. */
static void ready_push(struct rt_fiber_queue *q, struct rt_fiber *t) {
  t->ready_next = nullptr;
  if (q->tail == nullptr) {
    q->head = t;
  } else {
    q->tail->ready_next = t;
  }
  q->tail = t;
  q->count++;
}

/* Only ever called against a snapshot of count, so the queue is known
 * non-empty; an empty pop would be a bug in the loop rather than a case to
 * handle. The unlink clears ready_next: a stale link out of a fiber that is no
 * longer queued is exactly the kind of thing that makes a corrupted queue
 * look plausible while walking it. */
static struct rt_fiber *ready_pop(struct rt_fiber_queue *q) {
  struct rt_fiber *t = q->head;

  q->head = t->ready_next;
  if (q->head == nullptr) {
    q->tail = nullptr;
  }
  t->ready_next = nullptr;
  q->count--;

  return t;
}

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
  ready_push(&s->ready, t);
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
  rt_switch(&s->context, &t->ctx);

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
      ready_push(&s->ready, t);
      break;

    case RT_REQUEST_IO:
      ready_push(&s->submit, t);
      break;

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
      struct rt_fiber *t = ready_pop(&s->ready);

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

      struct rt_fiber *t = ready_pop(&s->submit);
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

    /* Nothing runnable and nothing outstanding: no completion can arrive, so
     * no fiber can ever become ready again. Today that is unreachable — every
     * suspension stages an SQE — and it becomes a real diagnosis only once
     * fibers can wait on each other. Until then it is a tripwire, not a
     * deadlock report, and it cannot fire for an idle server: a fiber parked
     * on RECV with no traffic is counted in inflight. */
    if (wait && s->inflight_count == 0) {
      rt_panic("scheduler: no fiber runnable and nothing in flight",
               __builtin_return_address(0));
    }

    if (staged > 0 || wait) {
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
      ready_push(&s->ready, t);
    }
  }
}
