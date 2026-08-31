#include <errno.h>
#include <unistd.h>

int main(void) {
  errno = 11;

  bool ok = close(0) == 0;
  ok = ok && errno == 11;

  const int again = close(0);
  ok = ok && again == -1 && errno == EBADF;

  return ok ? 0 : 122;
}
