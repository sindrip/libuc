/* The four functions the compiler assumes exist even under -ffreestanding. */

#include <stdint.h>
#include <string.h>

void *memcpy(void *restrict dst, const void *restrict src, size_t n) {
  unsigned char *restrict d = dst;
  const unsigned char *restrict s = src;
  for (size_t i = 0; i < n; i++) {
    d[i] = s[i];
  }
  return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
  unsigned char *d = dst;
  const unsigned char *s = src;
  if ((uintptr_t)d < (uintptr_t)s) {
    for (size_t i = 0; i < n; i++) {
      d[i] = s[i];
    }
  } else {
    for (size_t i = n; i > 0; i--) {
      d[i - 1] = s[i - 1];
    }
  }
  return dst;
}

void *memset(void *dst, int value, size_t n) {
  unsigned char *d = dst;
  for (size_t i = 0; i < n; i++) {
    d[i] = (unsigned char)value;
  }
  return dst;
}

int memcmp(const void *lhs, const void *rhs, size_t n) {
  const unsigned char *a = lhs;
  const unsigned char *b = rhs;
  for (size_t i = 0; i < n; i++) {
    if (a[i] != b[i]) {
      return a[i] < b[i] ? -1 : 1;
    }
  }
  return 0;
}
