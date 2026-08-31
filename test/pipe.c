#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static bool pipes_ok = true;

static void check(bool ok) { pipes_ok = pipes_ok && ok; }

int main(void) {
  errno = 11;

  int plain[2] = {-1, -1};
  check(pipe(plain) == 0);
  check(plain[0] >= 0 && plain[1] >= 0 && plain[0] != plain[1]);

  int cloexec[2] = {-1, -1};
  check(pipe2(cloexec, O_CLOEXEC) == 0);
  check(cloexec[0] >= 0 && cloexec[1] >= 0);

  check(errno == 11);

  int untouched[2] = {-1, -1};
  check(pipe2(untouched, 1) == -1);
  check(errno == EINVAL);
  check(untouched[0] == -1 && untouched[1] == -1);

  check(close(plain[0]) == 0 && close(plain[1]) == 0);
  check(close(cloexec[0]) == 0 && close(cloexec[1]) == 0);

  return pipes_ok ? 0 : 122;
}
