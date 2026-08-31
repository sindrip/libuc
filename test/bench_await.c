#include <stddef.h>
#include <stdint.h>

#include <linux/io_uring.h>

#include <unistd.h>

#include "bench_clock_arch.h"
#include "fiber/fiber.h"
#include "scheduler/scheduler.h"
#include "thread_local/thread_local.h"

constexpr size_t warmup_iterations = 1U << 12;
constexpr size_t single_iterations = 1U << 20;
constexpr size_t crowd_fibers = 64;
constexpr size_t crowd_iterations = 1U << 14;

static bool awaits_ok = true;

static void await_nops(size_t iterations) {
  for (size_t i = 0; i < iterations; i++) {
    const struct io_uring_sqe nop = {.opcode = IORING_OP_NOP};
    awaits_ok = awaits_ok && __libuc_fiber_await(&nop) == 0;
  }
}

static int single(void *opaque) {
  await_nops(*(const size_t *)opaque);

  return 0;
}

static int crowd([[maybe_unused]] void *opaque) {
  await_nops(crowd_iterations);

  return 0;
}

static char *append_string(char *cursor, const char *text) {
  while (*text != '\0') {
    *cursor++ = *text++;
  }
  return cursor;
}

static char *append_number(char *cursor, uint64_t value) {
  char digits[20];
  size_t count = 0;
  do {
    digits[count++] = (char)('0' + value % 10);
    value /= 10;
  } while (value != 0);
  while (count != 0) {
    *cursor++ = digits[--count];
  }
  return cursor;
}

static void report(const char *name, uint64_t ticks, uint64_t operations) {
  char line[128];
  char *cursor = append_string(line, name);

  cursor = append_string(cursor, ": ");
  cursor = append_number(cursor, ticks / operations);
  cursor = append_string(cursor, " ticks/op");

  const uint64_t frequency = bench_ticks_per_second();
  if (frequency != 0) {
    cursor = append_string(cursor, ", ");
    cursor = append_number(cursor, ticks * 1000000000 / frequency / operations);
    cursor = append_string(cursor, " ns/op");
  }
  *cursor++ = '\n';

  awaits_ok =
      awaits_ok && write(1, line, (size_t)(cursor - line)) == cursor - line;
}

int main(void) {
  /* 125 singles out an environment where user mode cannot install. */
  if (!__libuc_thread_local_install_available()) {
    return 125;
  }

  struct __libuc_scheduler scheduler;
  if (!__libuc_scheduler_become(&scheduler)) {
    return 124;
  }

  static struct __libuc_fiber *fibers[crowd_fibers];
  static size_t iterations;

  iterations = warmup_iterations;
  if ((fibers[0] = __libuc_fiber_spawn((size_t)64 * 1024, single,
                                       &iterations)) == nullptr) {
    return 123;
  }
  __libuc_scheduler_enqueue(&scheduler, fibers[0]);
  __libuc_scheduler_run(&scheduler);
  if (!__libuc_fiber_destroy(fibers[0])) {
    return 122;
  }

  iterations = single_iterations;
  if ((fibers[0] = __libuc_fiber_spawn((size_t)64 * 1024, single,
                                       &iterations)) == nullptr) {
    return 123;
  }
  __libuc_scheduler_enqueue(&scheduler, fibers[0]);
  const uint64_t single_start = bench_ticks();
  __libuc_scheduler_run(&scheduler);
  const uint64_t single_ticks = bench_ticks() - single_start;
  if (!__libuc_fiber_destroy(fibers[0])) {
    return 122;
  }

  for (size_t i = 0; i < crowd_fibers; i++) {
    if ((fibers[i] = __libuc_fiber_spawn((size_t)64 * 1024, crowd, nullptr)) ==
        nullptr) {
      return 121;
    }
    __libuc_scheduler_enqueue(&scheduler, fibers[i]);
  }
  const uint64_t crowd_start = bench_ticks();
  __libuc_scheduler_run(&scheduler);
  const uint64_t crowd_ticks = bench_ticks() - crowd_start;
  for (size_t i = 0; i < crowd_fibers; i++) {
    if (!__libuc_fiber_destroy(fibers[i])) {
      return 120;
    }
  }

  report("await-nop single", single_ticks, single_iterations);
  report("await-nop crowd-64", crowd_ticks, crowd_fibers * crowd_iterations);

  return awaits_ok ? 0 : 119;
}
