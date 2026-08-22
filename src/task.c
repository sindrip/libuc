/*
 * Task lifecycle: create, yield, exit, resume. switch.S does the register
 * save/restore; this file does the bookkeeping — allocating stacks, priming
 * a new context so the first switch lands in the trampoline, and tracking
 * which task is running.
 */
#include "task.h"

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

/* The scheduler is not a task: it is rt_main, running on the original
 * process stack, and its register state lives here. rt_current is how a
 * suspending task finds itself without being handed a pointer. */
struct rt_ctx rt_sched_ctx;
struct rt_task *rt_current;

void rt_task_create(struct rt_task *t, void (*fn)(void *), void *arg) {
  /* One mapping, entirely PROT_NONE, then open only the usable part: the
   * bottom page is never made accessible at all, so an overflow faults at a
   * known address instead of quietly corrupting whatever lies below. */
  long base = sys_mmap(nullptr, RT_GUARD_SIZE + RT_STACK_SIZE, PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (sys_failed(base)) {
    sys_exit_group((int)-base);
  }

  t->stack_base = (void *)(uintptr_t)base;
  t->stack_len = RT_GUARD_SIZE + RT_STACK_SIZE;
  t->fn = fn;
  t->arg = arg;

  long ret = sys_mprotect((char *)t->stack_base + RT_GUARD_SIZE, RT_STACK_SIZE,
                          PROT_READ | PROT_WRITE);
  if (sys_failed(ret)) {
    sys_exit_group((int)-ret);
  }

  /* The caller's struct rt_task is an uninitialized local, so start from a
   * known state instead of trusting whatever was on its stack. */
  t->ctx = (struct rt_ctx){};

  /* fp actively zero, not merely unset: the crash handler's frame walk
   * terminates on a null fp, and a stale value would send it wandering into
   * garbage. sp is the top of the usable region — stacks grow down — and is
   * 16-byte aligned by the static_assert above. lr makes the first ret out
   * of rt_switch land in the trampoline. */
  t->ctx.fp = 0;
  t->ctx.sp =
      (unsigned long)(uintptr_t)t->stack_base + RT_GUARD_SIZE + RT_STACK_SIZE;
  t->ctx.lr = (unsigned long)(uintptr_t)rt_task_entry;

  /* gp[0] and gp[1] are x19 and x20 — the only channel into the trampoline,
   * which does `blr x19` after `mov x0, x20`. */
  t->ctx.gp[0] = (unsigned long)(uintptr_t)fn;
  t->ctx.gp[1] = (unsigned long)(uintptr_t)arg;

  t->state = RT_READY;
}

/* Suspend voluntarily, still runnable. rt_switch saves x30 before restoring
 * the other side, so when the scheduler later switches back, control
 * returns here and rt_yield returns to whoever called it — a task cannot
 * tell it was ever descheduled. */
void rt_yield(void) {
  rt_current->state = RT_READY;
  rt_switch(&rt_current->ctx, &rt_sched_ctx);
}

/* The trampoline branches here when a task function returns. Non-return
 * rests on scheduler discipline — the loop stops resuming at RT_DEAD — and
 * nothing outside this file enforces that, so the function ends in a trap
 * rather than merely asserting the point unreachable: break the discipline
 * and it is a brk at a known address, not a fall-through into whatever the
 * linker placed next. */
[[noreturn]] void rt_task_exit(void) {
  rt_current->state = RT_DEAD;
  rt_switch(&rt_current->ctx, &rt_sched_ctx);

  __builtin_trap();
}

/* The scheduler's half of the switch; returns when the task suspends or
 * dies. Argument order is the trap: rt_switch(from, to) saves into `from`,
 * so reversing them overwrites the task's primed context with the
 * scheduler's registers and then jumps into a context nobody ever built —
 * the symptom is a fault at an address resembling nothing in the program. */
void rt_sched_resume(struct rt_task *t) {
  rt_current = t;
  rt_current->state = RT_RUNNING;
  rt_switch(&rt_sched_ctx, &rt_current->ctx);
}
