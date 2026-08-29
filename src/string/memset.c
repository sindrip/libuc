#include <string.h>

void *memset(void *dst, int value, size_t n) {
  unsigned char *d = dst;
  for (size_t i = 0; i < n; i++) {
    d[i] = (unsigned char)value;
  }
  return dst;
}
