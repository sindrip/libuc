/*
 * Stackful cooperative tasks. A task is a function with its own mmap'd
 * stack; it runs until it suspends — rt_yield, or a ring op parking it on a
 * CQE — and there is no preemption (invariant 7). The switch itself is
 * rt_switch (arch/aarch64/switch.c), which swaps the callee-saved registers
 * and sp between two struct rt_ctx values.
 */
#ifndef RT_TASK_H
#define RT_TASK_H

#include <stddef.h>

/* The saved register state, and the single authority on the switch's
 * layout: rt_switch (arch/aarch64/switch.c) computes every store/load
 * offset from these fields via offsetof, so the struct is free to change
 * shape and the asm follows — there is no offset table to keep in sync.
 * The one cross-member assumption the offsets cannot express — fp and lr
 * adjacent, for the x29/x30 stp pair — is asserted in switch.c. */
struct rt_ctx {
  unsigned long gp[10]; /* x19-x28 */
  unsigned long fp;     /* x29 */
  unsigned long lr;     /* x30 */
  unsigned long sp;
  double d[8]; /* d8-d15 */
};

/* Values are pinned rather than left to declaration order, as the kernel
 * pins TASK_RUNNING (include/linux/sched.h:107). Zero meaning RT_READY is
 * deliberate: a struct rt_task that was zeroed and never created also has
 * ctx.lr == 0, so resuming it faults at address 0 — an unambiguous report,
 * where a non-runnable zero would instead skip it in silence. A state added
 * later must not push RT_READY off zero.
 *
 * RT_BLOCKED is a task parked on a CQE: skipped by the ready scan, made
 * RT_READY only by the reap loop. */
enum rt_task_state : int {
  RT_READY = 0,
  RT_RUNNING = 1,
  RT_BLOCKED = 2,
  RT_DEAD = 3
};

struct rt_task {
  struct rt_ctx ctx;
  void *stack_base; /* the mmap base, INCLUDING the guard page */
  size_t stack_len; /* total mapping length (guard + usable) */
  enum rt_task_state state;
  int result; /* the reaper's delivery: cqe->res, already in place when a
                 blocked task resumes */
  void (*fn)(void *);
  void *arg;
};

/* Allocate a guarded stack, prime the context so the first switch into this
 * task enters the trampoline, and mark it RT_READY. */
void rt_task_create(struct rt_task *t, void (*fn)(void *), void *arg);

/* Give up the CPU voluntarily, still runnable; a later ready scan resumes
 * the task. Only for runnable tasks — a task suspending on a ring op goes
 * through sched.c, which marks it RT_BLOCKED instead. */
void rt_yield(void);

/* The raw switch (arch/aarch64/switch.c): saves the callee-saved state
 * into *from, restores *to, and returns as *to. */
extern void rt_switch(struct rt_ctx *from, struct rt_ctx *to);

/* Run one task until it suspends or dies. The asymmetry with rt_yield is
 * deliberate: a suspending task always knows where to return
 * (rt_sched_ctx), but the scheduler must be told which task to enter —
 * which is why rt_current exists at all. */
extern void rt_sched_resume(struct rt_task *t);

/* The running task and the scheduler's context. Ring ops suspend by
 * switching to rt_sched_ctx directly, bypassing rt_yield — rt_yield would
 * mark the task RT_READY, and only the reap loop may do that to a blocked
 * task. */
extern struct rt_ctx rt_sched_ctx;
extern struct rt_task *rt_current;

/* First-entry trampoline (switch.c): calls fn(arg) out of x19/x20, then
 * branches to rt_task_exit. */
extern void rt_task_entry(void);

/* Marks the returning task RT_DEAD and switches to the scheduler for the
 * last time. */
[[noreturn]] extern void rt_task_exit(void);

#endif /* RT_TASK_H */
