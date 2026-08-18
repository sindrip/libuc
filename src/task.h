/*
 * Task management — the data structures and operations for stackful
 * coroutines.
 *
 * A "task" here is a function with its own stack. It runs until it
 * voluntarily yields (there is no preemption — invariant 7), at which
 * point the scheduler resumes another task. The mechanism is rt_switch
 * (switch.S), which swaps the callee-saved registers and sp between two
 * struct rt_ctx values.
 *
 * This ticket (RT-004) is deliberately isolated from the ring. If
 * something breaks, it is the switch or the stack — not I/O.
 */
#ifndef RT_TASK_H
#define RT_TASK_H

#include <stdint.h>

/* TODO(1): Define struct rt_ctx — the saved register state.
 *
 * This struct is the C-side mirror of what switch.S saves and restores.
 * The field order and sizes must match switch.S's offsets *exactly*, or
 * the switch silently corrupts registers.
 *
 * Layout (see the offset table in switch.S):
 *   unsigned long gp[10]   — x19–x28 (10 × 8 = 80 bytes, offset 0)
 *   unsigned long fp       — x29, offset 80
 *   unsigned long lr       — x30, offset 88
 *   unsigned long sp       — offset 96
 *   double        d[8]     — d8–d15 (8 × 8 = 64 bytes, offset 104)
 *
 * Total: 168 bytes. Add a static_assert that sizeof is 168 — if the
 * compiler inserts padding, the struct and asm disagree and the switch
 * silently corrupts state.
 *
 * 168, not 176 — do not "round up for 16-byte alignment". That instinct
 * confuses two different rules that share a number:
 *
 *   - `sp` must be 16-byte aligned at every instruction that uses it.
 *     That is architectural, and it is why start.S does `and sp, x0, #-16`.
 *   - `stp`/`ldp` require no such thing. On Normal memory they permit
 *     unaligned access outright; the struct's natural 8-byte alignment is
 *     already more than the instruction asks for.
 *
 * rt_ctx is never used *as* a stack — it is a plain struct that x0/x1
 * point at — so the sp rule does not apply to it. Ground truth: the
 * kernel's own cpu_context (arch/arm64/include/asm/processor.h:136-150)
 * is 13 unsigned longs = 104 bytes, not a multiple of 16, and it is
 * exactly what cpu_switch_to (entry.S:821) drives with stp/ldp.
 */
struct rt_ctx {
  unsigned long gp[10];
  unsigned long fp;
  unsigned long lr;
  unsigned long sp;
  double d[8];
};

static_assert(sizeof(struct rt_ctx) == 168);

/* TODO(2): Define the task states.
 *
 * enum rt_task_state { RT_READY, RT_RUNNING, RT_DEAD }
 *
 * RT_BLOCKED appears in the ticket spec for a task waiting on I/O.
 * Nothing until RT-006 can block, so it is not defined here yet.
 */
/* Values are pinned rather than left to declaration order, as the kernel
 * pins TASK_RUNNING (include/linux/sched.h:107). Zero meaning RT_READY is
 * deliberate: a struct rt_task that was zeroed and never created also has
 * ctx.lr == 0, so resuming it faults at address 0 — an unambiguous report,
 * where a non-runnable zero would instead make rt_main skip it in silence.
 * A state added later must not push RT_READY off zero. */
enum rt_task_state { RT_READY = 0, RT_RUNNING = 1, RT_DEAD = 2 };

/* TODO(3): Define struct rt_task.
 *
 * Fields:
 *   struct rt_ctx  ctx         — saved register context
 *   void          *stack_base  — the mmap base, INCLUDING the guard page
 *   unsigned long  stack_len   — total mmap length (guard + usable)
 *   enum rt_task_state state
 *   void (*fn)(void *)         — the task's entry function
 *   void *arg                  — argument passed to fn
 */
struct rt_task {
  struct rt_ctx ctx;
  void *stack_base;
  unsigned long stack_len;
  enum rt_task_state state;
  void (*fn)(void *);
  void *arg;
};

/* ---------------------------------------------------------------------------
 * Functions — implemented in task.c
 * ------------------------------------------------------------------------- */

/* TODO(4): Declare rt_task_create.
 *
 * void rt_task_create(struct rt_task *t, void (*fn)(void *), void *arg);
 *
 * Allocates a stack (mmap + guard page), primes the context so that the
 * first rt_switch into this task enters rt_task_entry, and sets state
 * to RT_READY.
 */
void rt_task_create(struct rt_task *t, void (*fn)(void *), void *arg);

/* TODO(5): Declare rt_yield.
 *
 * void rt_yield(void);
 *
 * Called by a running task to voluntarily give up the CPU. Switches
 * back to the scheduler's context.
 */
void rt_yield(void);

/* TODO(6): Declare rt_switch (implemented in switch.S).
 *
 * void rt_switch(struct rt_ctx *from, struct rt_ctx *to);
 *
 * The raw context switch. rt_yield and the scheduler call this;
 * task code calls rt_yield instead.
 */
extern void rt_switch(struct rt_ctx *from, struct rt_ctx *to);

/* TODO(7): Declare rt_sched_resume.
 *
 * void rt_sched_resume(struct rt_task *t);
 *
 * The scheduler side of the switch: rt_main calls this to run a task,
 * and it returns once that task yields or dies.
 *
 * Note the asymmetry with rt_yield, which takes no argument. A task
 * always knows which context to return to (the scheduler's, which is
 * file-static in task.c), but the scheduler must be told which task to
 * enter. That asymmetry is why `current` exists in task.c — it is how
 * rt_yield finds the task that is running without being handed it.
 */
extern void rt_sched_resume(struct rt_task *t);

/* rt_task_entry is in switch.S — the trampoline for first entry. */
extern void rt_task_entry(void);

/* rt_task_exit is in task.c — called by the trampoline when a task returns. */
[[noreturn]] extern void rt_task_exit(void);

#endif /* RT_TASK_H */
