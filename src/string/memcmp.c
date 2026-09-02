#include <string.h>

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);
typedef unsigned long long scalar;

/* How far a scan runs past a difference before noticing. */
typedef scalar block [[gnu::vector_size(64)]];

int memcmp(const void *lhs, const void *rhs, size_t n) {
  const unsigned char *l = lhs;
  const unsigned char *r = rhs;

  /* The break skips the decrement; the difference is in the first block. */
  while (n >= sizeof(block)) {
    block a;
    block b;
    __builtin_memcpy(&a, l, sizeof(a));
    __builtin_memcpy(&b, r, sizeof(b));
    if (__builtin_reduce_or(a ^ b) != 0) {
      break;
    }
    l += sizeof(block);
    r += sizeof(block);
    n -= sizeof(block);
  }

  /* Lexicographic byte order is big-endian numeric order. */
  while (n >= sizeof(scalar)) {
    scalar a;
    scalar b;
    __builtin_memcpy(&a, l, sizeof(a));
    __builtin_memcpy(&b, r, sizeof(b));
    if (a != b) {
      return __builtin_bswapg(a) < __builtin_bswapg(b) ? -1 : 1;
    }
    l += sizeof(scalar);
    r += sizeof(scalar);
    n -= sizeof(scalar);
  }

  for (size_t i = 0; i < n; i++) {
    if (l[i] != r[i]) {
      return l[i] < r[i] ? -1 : 1;
    }
  }

  return 0;
}
