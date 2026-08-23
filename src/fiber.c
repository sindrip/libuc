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

#include "syscall.h"

/* Stack geometry. One guard page below the usable region. */
constexpr size_t RT_PAGE_SIZE = 4096;
constexpr size_t RT_GUARD_SIZE = RT_PAGE_SIZE;
constexpr size_t RT_STACK_SIZE = 64 * 1024;

/* Both properties are accidents of the numbers above, not guarantees, and
 * neither fails visibly. mmap promises page alignment and nothing more. */
static_assert(RT_GUARD_SIZE % RT_PAGE_SIZE == 0,
              "mprotect needs a page-aligned address, and it is given "
              "stack_base + RT_GUARD_SIZE");
static_assert((RT_GUARD_SIZE + RT_STACK_SIZE) % 16 == 0,
              "the initial sp is stack_base + this sum, and aarch64 faults on "
              "an unaligned sp at every instruction that uses it");

void rt_fiber_create(struct rt_fiber *f, void (*fn)(void *), void *arg) {
  /* One mapping, entirely PROT_NONE, then open only the usable part: the
   * bottom page is never made accessible at all, so an overflow faults at a
   * known address instead of quietly corrupting whatever lies below. */
  auto base = sys_mmap(nullptr, RT_GUARD_SIZE + RT_STACK_SIZE, PROT_NONE,
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
  f->stack_len = RT_GUARD_SIZE + RT_STACK_SIZE;
  f->fn = fn;
  f->arg = arg;

  auto ret = sys_mprotect((char *)f->stack_base + RT_GUARD_SIZE, RT_STACK_SIZE,
                          PROT_READ | PROT_WRITE);
  if (sys_failed(ret)) {
    sys_exit_group(-ret);
  }

  /* fp actively zero, not merely unset: the crash handler's frame walk
   * terminates on a null fp, and a stale value would send it wandering into
   * garbage. sp is the top of the usable region — stacks grow down — and is
   * 16-byte aligned by the static_assert above. lr makes the first ret out
   * of rt_switch land in the trampoline. */
  f->ctx.fp = 0;
  f->ctx.sp =
      (unsigned long)(uintptr_t)f->stack_base + RT_GUARD_SIZE + RT_STACK_SIZE;
  f->ctx.lr = (unsigned long)(uintptr_t)rt_fiber_entry;

  /* gp[0] and gp[1] are x19 and x20 — the only channel into the trampoline,
   * which does `blr x19` after `mov x0, x20`. */
  f->ctx.gp[0] = (unsigned long)(uintptr_t)fn;
  f->ctx.gp[1] = (unsigned long)(uintptr_t)arg;
}

static struct rt_fiber *current;

struct rt_fiber *rt_fiber_current(void) { return current; }
void rt_fiber_set_current(struct rt_fiber *f) { current = f; }

/* The whole boundary, in two lines. The request was set by the caller and is
 * the message to the scheduler; suspend_to is where control goes. The return
 * runs arbitrarily later, with the CQE's res already delivered into result,
 * and the caller cannot tell it was ever gone.
 *
 * Nothing here writes state. What a fiber *is* while suspended is the
 * scheduler's to decide from what the fiber *asked for*, and a field with one
 * writer cannot disagree with itself. */
static int rt_fiber_suspend(struct rt_fiber *self) {
  rt_switch(&self->ctx, self->suspend_to);

  return self->result;
}

void rt_fiber_yield(void) {
  struct rt_fiber *self = rt_fiber_current();

  self->request = (struct rt_fiber_request){.kind = RT_REQUEST_YIELD};
  (void)rt_fiber_suspend(self);
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

  return rt_fiber_suspend(self);
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
  (void)rt_fiber_suspend(self);

  __builtin_trap();
}
