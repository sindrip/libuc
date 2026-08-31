#ifndef LIBUC_THREADS_H
#define LIBUC_THREADS_H

#include <stddef.h>

struct __libuc_fiber;

typedef struct __libuc_fiber *thrd_t;
typedef int (*thrd_start_t)(void *);

enum {
  thrd_success = 0,
  thrd_busy = 1,
  thrd_error = 2,
  thrd_nomem = 3,
  thrd_timedout = 4,
};

int thrd_create(thrd_t *thr, thrd_start_t func, void *arg);
thrd_t thrd_current(void);
int thrd_equal(thrd_t lhs, thrd_t rhs);
[[noreturn]] void thrd_exit(int res);
int thrd_join(thrd_t thr, int *res);
void thrd_yield(void);

#endif
