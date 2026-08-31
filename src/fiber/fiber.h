#ifndef LIBUC_SRC_FIBER_FIBER_H
#define LIBUC_SRC_FIBER_FIBER_H

#include <stddef.h>
#include <stdint.h>

#include "fiber_arch.h"
#include "thread_local/thread_local.h"

/* Zero is not a kind: it is what a resume passes in, so seeing it come
 * back means a broken transfer. */
enum __libuc_fiber_request_kind : unsigned long {
  __LIBUC_FIBER_REQUEST_YIELD = 1,
  __LIBUC_FIBER_REQUEST_EXIT,
  __LIBUC_FIBER_REQUEST_AWAIT,
  __LIBUC_FIBER_REQUEST_SPAWN,
  __LIBUC_FIBER_REQUEST_JOIN,
  __LIBUC_FIBER_REQUEST_DETACH,
};

struct io_uring_sqe;

/* A SPAWN request's argument. */
struct __libuc_fiber_spawn_request {
  size_t stack_length;
  int (*entry)(void *);
  void *argument;
};

/* What a suspended fiber is asking for. It lives on the asking fiber's
 * frame, which the suspension keeps alive, and the switch carries its
 * address — so nothing about a request is readable outside the window
 * in which it means something. */
struct __libuc_fiber_request {
  enum __libuc_fiber_request_kind kind;
  union {
    /* An SQE for AWAIT, a spawn request for SPAWN. */
    const void *argument;
    /* JOIN's ask: the fiber to wait on. Its record belongs to the
     * scheduler answering the request, so it rides unqualified. */
    struct __libuc_fiber *target;
  };
  /* The scheduler's answer, written before it resumes the asker. */
  long result;
  /* Stamped by the scheduler when it takes the request, so an answer
   * that arrives later knows whom to resume. */
  struct __libuc_fiber *fiber;
};

/* A record outlives its exit: the mapping keeps the status readable
 * until a join or detach takes it. A detached fiber skips the zombie:
 * its exit reclaims it at once. Spawn zero-initializes the record, so
 * live must be the zero state. */
enum __libuc_fiber_life : uint32_t {
  __LIBUC_FIBER_LIVE,
  __LIBUC_FIBER_DETACHED,
  __LIBUC_FIBER_EXITED,
};

static_assert(__LIBUC_FIBER_LIVE == 0);

struct __libuc_fiber {
  /* where the fiber stopped */
  struct fiber_context context;
  int (*entry)(void *);
  void *argument;
  unsigned char *stack;
  size_t stack_length;
  struct __libuc_thread_local_block thread_local_block;
  /* The pending resume's frame; dead between resumes. */
  struct fiber_context *return_to;
  /* Scheduler-owned FIFO link; a fiber is in at most one queue. */
  struct __libuc_fiber *ready_next;
  /* The one join waiting on this fiber, parked until the exit that
   * answers it; scheduler-owned like the FIFO link. A second joiner
   * traps. */
  struct __libuc_fiber_request *joiner;
  /* The exit status, readable once the fiber has exited. */
  int status;
  enum __libuc_fiber_life life;
};

/* Create a fiber whose record lives at the top of its own stack mapping,
 * so the stack grows down away from it. nullptr when the mapping fails or
 * the stack cannot hold the record; destroying the fiber invalidates the
 * pointer along with the mapping. */
[[nodiscard]] struct __libuc_fiber *
__libuc_fiber_spawn(size_t stack_length, int (*entry)(void *), void *argument);

/* Spawn on the running fiber's own scheduler and make the result
 * runnable; nullptr when the fiber cannot be created. */
[[nodiscard]] struct __libuc_fiber *
__libuc_fiber_start(size_t stack_length, int (*entry)(void *),
                    void *argument);

[[nodiscard]] bool __libuc_fiber_destroy(const struct __libuc_fiber *fiber);

/* Run the fiber until it suspends or exits, and give back the request
 * it carried. EXIT poisons the context, so resuming again
 * faults. */
[[nodiscard]] struct __libuc_fiber_request *
__libuc_fiber_resume(struct __libuc_fiber *fiber);

/* Suspend the running fiber back to its resumer; with no current fiber
 * this faults. */
void __libuc_fiber_yield(void);

/* End the running fiber with this status, from any call depth, exactly
 * as returning it from the entry does; with no current fiber this
 * faults. */
[[noreturn]] void __libuc_fiber_exit(int status);

/* Block until the target exits, then take its status; the join
 * releases the target's record, so the handle is dead afterward.
 * Joining yourself, or a target another fiber already joins, traps.
 * With no current fiber this faults. */
[[nodiscard]] long __libuc_fiber_join(struct __libuc_fiber *target);

/* Give up the right to join the target: a live target is reclaimed by
 * its exit, an exited one right now; either way the handle is dead
 * afterward. Detaching a target twice, or one a fiber joins, traps;
 * detaching yourself is allowed. With no current fiber this faults. */
void __libuc_fiber_detach(struct __libuc_fiber *target);

/* Exactly one CQE; the reap loop traps streams. The suspended frame
 * keeps the SQE and its buffers live through the CQE. Returns res; with
 * no current fiber this faults. */
[[nodiscard]] long __libuc_fiber_await(const struct io_uring_sqe *sqe);

/* The running fiber; nullptr where no block is installed. */
[[nodiscard]] struct __libuc_fiber *__libuc_fiber_current(void);

#endif
