/*
 * Fiber bodies: birth, identity, and the three ways to give up the CPU.
 * Contracts are in fiber.h.
 *
 * Two boundaries live here and nothing else does. rt_fiber_suspend is the
 * transfer itself — the only rt_switch call on the fiber side of the runtime,
 * with rt_scheduler_resume the only other in the program. rt_fiber_await_io is
 * the only place a pointer-bearing request is published, which is why the
 * lifetime argument C cannot type is stated once, at its contract in fiber.h,
 * instead of nine times in io.c.
 *
 * Something running as the fiber has to save the fiber's callee-saved
 * registers, since invariant 7 rules out doing it from outside. But it need
 * only appear once: yield, an awaited operation and death differ in the
 * request they leave behind, not in the transfer.
 */

#include "fiber.h"

#include <stddef.h>
#include <stdint.h>

/* linux/mman.h, not asm/mman.h: MAP_SHARED/MAP_PRIVATE/MAP_SHARED_VALIDATE
 * (0x01-0x03) are the three flags an arch may not override, so they live here
 * rather than in asm-generic/mman-common.h, which says so at its line 20.
 * linux/mman.h includes asm/mman.h itself, so this covers PROT_* too. */
#include <linux/mman.h>

#include "auxv.h"
#include "crash.h"
#include "syscall.h"

/* Stack geometry. One guard page below the usable region.
 *
 * The guard is exactly one page and the page size comes from the kernel
 * (auxv.h), so the mprotect boundary is aligned by construction rather than
 * by a constant that happens to match. Only the size is ours to choose, and
 * choosing it is a .scratch/stacks.md question — 64 KiB is RT-004's guess,
 * not a measurement, and it is the same number for every architecture because
 * nothing about it is derived from one.
 *
 * sp alignment is deliberately absent here: how a context starts is
 * rt_ctx_init's business (context.h), which is what keeps this file free of
 * aarch64. */
constexpr size_t RT_STACK_SIZE = 64 * 1024;

void rt_fiber_create(struct rt_fiber *f, void (*fn)(void *), void *arg) {
  const size_t guard = rt_page_size();

  /* mprotect rounds a length up to a page but demands an aligned address, and
   * a stack whose usable region is not a whole number of pages would put the
   * two ends on different sides of that rule.
   *
   * A mask, not a modulo: rt_page_size has already established the power of
   * two, and the division form makes UBSan emit a divrem-overflow check for a
   * denominator that cannot be zero. Cannot fire on aarch64, where
   * every supported page size divides 64 KiB — it exists so that changing
   * RT_STACK_SIZE fails loudly rather than subtly. */
  if ((RT_STACK_SIZE & (guard - 1)) != 0) {
    rt_panic("fiber: stack size is not a multiple of the page size",
             __builtin_return_address(0));
  }

  /* One mapping, entirely PROT_NONE, then open only the usable part: the
   * bottom page is never made accessible at all, so an overflow faults at a
   * known address instead of quietly corrupting whatever lies below. */
  auto base = sys_mmap(nullptr, guard + RT_STACK_SIZE, PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (sys_failed(base)) {
    sys_exit_group((int)-base);
  }

  /* Whole-struct zero, not field by field. Every field whose correct initial
   * value is zero gets it without being named: no completion owed, on no
   * queue, no request, and a null suspend_to — so a fiber resumed by nothing
   * switches to address zero and faults, rather than wandering into whatever
   * was on the caller's stack. The caller's struct rt_fiber is an
   * uninitialized local, so this is also what stops it inheriting garbage. */
  *f = (struct rt_fiber){};

  f->stack_base = (void *)(uintptr_t)base;
  f->stack_len = guard + RT_STACK_SIZE;
  f->fn = fn;
  f->arg = arg;

  auto ret = sys_mprotect((char *)f->stack_base + guard, RT_STACK_SIZE,
                          PROT_READ | PROT_WRITE);
  if (sys_failed(ret)) {
    sys_exit_group(-ret);
  }

  /* Everything about how a context starts — which registers carry fn and
   * arg, that control resumes through a link register, what sp must be
   * aligned to — is arch/'s. This file supplies memory and a function. */
  rt_ctx_init(&f->ctx, (char *)f->stack_base + f->stack_len, fn, arg);
}

/* FIFO. Push at the tail so a yielding fiber goes behind everything already
 * waiting — round-robin falls out, and a fiber that only yields cannot hold
 * the CPU against its peers. */
void rt_fiber_queue_push(struct rt_fiber_queue *q, struct rt_fiber *t) {
  t->ready_next = nullptr;
  if (q->tail == nullptr) {
    q->head = t;
  } else {
    q->tail->ready_next = t;
  }
  q->tail = t;
  q->count++;
}

/* nullptr on empty, which is an answer and not a failure: unlocking a mutex
 * nobody is waiting on is the common case, and the scheduler's own callers
 * pop against a snapshot of count and cannot see it.
 *
 * The unlink clears ready_next: a stale link out of a fiber that is no longer
 * queued is exactly the kind of thing that makes a corrupted queue look
 * plausible while walking it. */
struct rt_fiber *rt_fiber_queue_pop(struct rt_fiber_queue *q) {
  struct rt_fiber *t = q->head;

  if (t == nullptr) {
    return nullptr;
  }

  q->head = t->ready_next;
  if (q->head == nullptr) {
    q->tail = nullptr;
  }
  t->ready_next = nullptr;
  q->count--;

  return t;
}

static struct rt_fiber *current;

struct rt_fiber *rt_fiber_current(void) { return current; }
void rt_fiber_set_current(struct rt_fiber *f) { current = f; }

/* The whole boundary, in one line. The request was set by the caller and is
 * the message to the scheduler; suspend_to is where control goes. Control
 * returns arbitrarily later and the caller cannot tell it was ever gone.
 *
 * void, not int. Only an awaited operation has a result, so only that path
 * reads self->result — a yield returning "the answer to whatever this fiber
 * last asked for" is a value with no meaning, and one its caller would have
 * to remember to discard.
 *
 * Nothing here writes state either. What a fiber *is* while suspended is the
 * scheduler's to decide from what the fiber *asked for*. */
static void rt_fiber_suspend(struct rt_fiber *self) {
  rt_switch(&self->ctx, self->suspend_to);
}

void rt_fiber_yield(void) {
  struct rt_fiber *self = rt_fiber_current();

  self->request = (struct rt_fiber_request){.kind = RT_REQUEST_YIELD};
  rt_fiber_suspend(self);
}

void rt_fiber_park(struct rt_fiber_queue *q) {
  struct rt_fiber *self = rt_fiber_current();

  self->request = (struct rt_fiber_request){
      .kind = RT_REQUEST_PARK,
      .value = {.wait_queue = q},
  };
  rt_fiber_suspend(self);
}

/* Suspends in the mechanical sense — it switches — but not in the scheduling
 * sense: rt_scheduler_resume services this and switches straight back, so the
 * caller keeps the CPU and never re-enters the ready queue. */
void rt_fiber_wake(struct rt_fiber *f) {
  struct rt_fiber *self = rt_fiber_current();

  self->request = (struct rt_fiber_request){
      .kind = RT_REQUEST_WAKE,
      .value = {.wake = f},
  };
  rt_fiber_suspend(self);
}

/* The request and everything it references must remain valid until this
 * returns, and it never returns before the request's CQE is reaped. The full
 * argument, and the two things that will eventually break it, are at the
 * declaration in fiber.h. */
int rt_fiber_await_io(struct io_uring_sqe sqe) {
  struct rt_fiber *self = rt_fiber_current();

  self->request = (struct rt_fiber_request){
      .kind = RT_REQUEST_IO,
      .value = {.io = sqe},
  };

  rt_fiber_suspend(self);

  return self->result;
}

/* Non-return rests on scheduler discipline — the loop stops resuming at
 * a fiber that requested EXIT — and nothing outside the scheduler enforces
 * that, so this ends in
 * a trap rather than merely asserting the point unreachable: break the
 * discipline and it is a brk at a known address, not a fall-through into
 * whatever the linker placed next. */
[[noreturn]] void rt_fiber_exit(void) {
  struct rt_fiber *self = rt_fiber_current();

  self->request = (struct rt_fiber_request){.kind = RT_REQUEST_EXIT};
  rt_fiber_suspend(self);

  __builtin_trap();
}
