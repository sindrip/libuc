/*
 * Task lifecycle — create, yield, exit.
 *
 * This file is the C half of the context-switch machinery. switch.S does
 * the register save/restore; this file does the bookkeeping: allocating
 * stacks, priming a new context so that rt_switch lands in the trampoline,
 * and tracking which task is running vs. which context is the scheduler.
 *
 * The scheduler in RT-004 is trivial — it lives in rt_main and manually
 * calls rt_switch. There is no run queue, no priority, no preemption.
 * That is intentional: this ticket tests the switch, not the scheduler.
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

/* ---------------------------------------------------------------------------
 * Scheduler context.
 *
 * When a task yields, it switches back to the scheduler. The scheduler is
 * not a task — it is rt_main, running on the original process stack. Its
 * register state lives here.
 *
 * `current` points to whatever task is presently running. rt_yield uses it
 * to know whose context to save.
 * ------------------------------------------------------------------------- */
static struct rt_ctx sched_ctx;
static struct rt_task *current;

/* TODO(1): Implement rt_task_create.
 *
 * Three steps:
 *
 * a) Allocate the stack, via the wrappers in syscall.h.
 *
 *    Take the guard page and the usable stack as a single anonymous,
 *    private mapping that starts out entirely inaccessible, then open
 *    only the usable part for reading and writing. Doing it in that
 *    order is what buys a guard page without a second mapping: the
 *    bottom page is never made accessible at all, so an overflow faults
 *    at a known address instead of quietly corrupting whatever lies
 *    below it.
 *
 *    Both calls report failure as -errno, in the range sys_failed()
 *    tests for — neither returns a pointer, so check before converting.
 *
 * b) Record stack_base and stack_len in the task struct.
 *
 * c) Prime the context.
 *    The task has never run, so there is no saved state to restore —
 *    you are manufacturing one for rt_switch to restore *into*.
 *
 *    Derive it from the other end rather than from a list: rt_switch's
 *    restore half ends in `ret`, and rt_task_entry expects the function
 *    and its argument in specific registers. Read both in switch.S,
 *    then fill t->ctx so the first switch into this task lands in the
 *    trampoline with a working stack. The offset table at the top of
 *    switch.S maps register names onto struct rt_ctx fields.
 *
 *    Three things that are easy to get wrong:
 *
 *      - the stack grows down, so sp starts at the *top* of the usable
 *        region — and aarch64 requires sp 16-byte aligned at every
 *        instruction that touches it.
 *
 *      - x29 must be actively zeroed, not merely left alone. RT-007's
 *        crash handler walks the frame-pointer chain and stops at null;
 *        a root frame carrying a stale x29 sends it wandering into
 *        another task's dead stack, and a backtrace that lies is worse
 *        than no backtrace.
 *
 *      - fn and arg are pointers going into unsigned long fields.
 *        (unsigned long)(uintptr_t) is the correct double cast:
 *        uintptr_t is the type guaranteed to round-trip a pointer, and
 *        the outer cast silences a width warning on a conversion the
 *        compiler has no way to know is safe.
 *
 *    Everything else in ctx must be zero, and will not be by default —
 *    rt_main declares its struct rt_task as a local.
 *
 * d) Record fn and arg, and set the state that makes rt_main's loop
 *    willing to resume this task.
 */
void rt_task_create(struct rt_task *t, void (*fn)(void *), void *arg) {
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
   * known state instead of trusting whatever was on rt_main's stack. */
  t->ctx = (struct rt_ctx){};

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

/* TODO(2): Implement rt_yield.
 *
 * The running task calls this to give up the CPU. Two things must
 * happen: it records that it is still runnable rather than finished —
 * compare the states in task.h and pick the one rt_main's loop will act
 * on — and control returns to the scheduler's context.
 *
 * Note what you are not given: the task pointer. Find it. This file
 * already tracks the running task, and that is the reason it does.
 *
 * The subtle part is the return path. rt_switch saves x30 before it
 * restores the other side, so when the scheduler switches back in,
 * rt_switch returns *here* and rt_yield returns to whoever called it.
 * A task cannot tell it was ever descheduled — that illusion is the
 * whole point of the ticket.
 */
void rt_yield(void) {
  current->state = RT_READY;
  rt_switch(&current->ctx, &sched_ctx);
}

/* TODO(3): Implement rt_task_exit.
 *
 * switch.S branches here when a task function returns. Look at the last
 * instruction of rt_task_entry and work out why it is a branch and not
 * a call — the answer is also why this function is [[noreturn]].
 *
 * The same two moves as rt_yield, with a different state: this task is
 * finished, and the scheduler must never resume it.
 *
 * Being [[noreturn]] means convincing the compiler that control does
 * not fall off the end. Non-return here rests on scheduler discipline —
 * rt_main stops resuming at RT_DEAD — and nothing outside this file
 * enforces that, so the function ends in a trap rather than merely
 * asserting the point is unreachable. Break the discipline and you get
 * a brk at a known address instead of a fall-through into whatever the
 * linker placed next.
 */
[[noreturn]] void rt_task_exit(void) {
  current->state = RT_DEAD;
  rt_switch(&current->ctx, &sched_ctx);

  __builtin_trap();
}

/* TODO(4): Implement rt_sched_resume.
 *
 * The scheduler's half of the switch, called from rt_main. Three
 * things: record which task is now running — rt_yield depends on this,
 * since it is the only way it can find its own task — set the state to
 * match reality, and switch from the scheduler's context into the
 * task's.
 *
 * Watch the direction of the arguments. rt_switch(from, to) saves into
 * `from` and restores from `to`. Reversing them here overwrites the
 * task's primed context with the scheduler's registers and then jumps
 * into a context nobody ever built. It is the most common way to break
 * this file, and the symptom is a fault at an address that resembles
 * nothing in the program.
 *
 * Returns when the task yields or exits.
 */
void rt_sched_resume(struct rt_task *t) {
  current = t;
  current->state = RT_RUNNING;
  rt_switch(&sched_ctx, &current->ctx);
}
