/*
 * RT-004 driver: prove the context switch works.
 *
 * One task writes 'A', yields, writes 'B', yields, writes 'C', then returns.
 * The scheduler emits a digit before each resume. RT-003's banner runs first.
 *
 * Every character is a proof:
 *   'rt: alive' — RT-003 substrate intact: start.S, syscall.h, raw_write
 *   '1' — scheduler ran, about to enter the task for the first time
 *   'A' — task started on its own stack, trampoline worked
 *   '2' — task yielded, scheduler regained control
 *   'B' — task resumed where it left off (x30 restore correct)
 *   '3' — second yield/resume cycle
 *   'C' — task's third run
 *   (then the task returns → trampoline branches to rt_task_exit →
 *    scheduler sees RT_DEAD → idle loop)
 *
 * The banner is part of the test rather than a leftover: every character
 * after it travels through raw_write -> sys3 -> svc #0, so RT-003's
 * acceptance check re-runs on every boot. It also separates the first two
 * failures below, which are otherwise one indistinguishable "no output".
 *
 * Reading a failure:
 *
 *   nothing at all      the substrate is broken, not the switch. syscall.h,
 *                       start.S, or the link. Debug RT-003, not task.c.
 *   "rt: alive" only    the scheduler never reached its first resume, or
 *                       rt_task_create faulted.
 *   "rt: alive1"        the first switch-in never landed in the task: check
 *                       the primed lr and sp in rt_task_create.
 *   "...1A" then fault  switch-in worked; the yield back corrupted something.
 *   "...1A2" no 'B'     the resume path failed.
 *   "...1A2B3C" fault   rt_task_exit is broken.
 *
 * Compile without linking: `make check`.
 */
#include "syscall.h"
#include "task.h"

/* TODO(1): Emit a single character.
 *
 * One byte to fd 1. raw_write wants a pointer and a length, so the
 * character needs an address.
 */
static void put(char c) { raw_write(1, &c, 1); }

/* TODO(2): The task body.
 *
 * 'A', yield, 'B', yield, 'C', then return.
 *
 * The return is the part under test. Nothing else in this ticket drives
 * rt_task_entry's `blr x19` back to the `b rt_task_exit` that follows it,
 * so a task that loops forever would pass every other check here.
 *
 * arg is unused.
 */
static void abc_task(void *arg) {
  (void)arg;

  put('A');
  rt_yield();
  put('B');
  rt_yield();
  put('C');
}

/* TODO(3): The scheduler.
 *
 * a) Already below: RT-003's banner, retained verbatim so that ticket's
 *    acceptance check re-runs on every boot.
 *
 *    It has to stay `static const char[]` rather than `const char *`:
 *    a pointer loses the array type, so `sizeof` becomes 8 and the
 *    length silently folds to nothing.
 *
 * b) Create one task, running abc_task.
 *
 * c) Loop, resuming the task until it reports itself finished, emitting
 *    one digit before each resume — '1', then '2', then '3'.
 *
 *    Deriving the digit from the iteration count is acceptable only
 *    because this test knows there are exactly three. Nothing else in
 *    this file should acquire that assumption.
 *
 * d) A newline for a clean console, then the idle loop — `wfe` forever,
 *    as in RT-003. Invariant 6: PID 1 must never return.
 *
 * `stack` is the pre-alignment stack pointer start.S leaves in x0
 * (start.S:44). Nothing in this ticket needs it.
 *
 * Expected console output:
 *
 *     rt: alive
 *     1A2B3C
 *
 * then silence.
 */
[[noreturn]] void rt_main(void *stack) {
  (void)stack;

  static const char banner[] = "rt: alive\n";
  raw_write(1, banner, sizeof banner - 1);

  struct rt_task t;
  rt_task_create(&t, abc_task, nullptr);

  int count = 0;
  while (t.state != RT_DEAD) {
    count++;
    put((char)('0' + count));
    rt_sched_resume(&t);
  }
  put('\n');

  for (;;) {
    __asm__ volatile("wfe");
  }
}
