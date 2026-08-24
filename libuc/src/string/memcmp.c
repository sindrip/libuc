#include <string.h>

#include "memcmp_arch.h"

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);

int memcmp(const void *lhs, const void *rhs, size_t n) {
  const unsigned char *l = lhs;
  const unsigned char *r = rhs;
  size_t remaining = n;

  /* The break skips the decrement; the difference is in the first window. */
  while (remaining >= memcmp_arch_block_size) {
    if (!memcmp_arch_equal(l, r)) {
      break;
    }
    l += memcmp_arch_block_size;
    r += memcmp_arch_block_size;
    remaining -= memcmp_arch_block_size;
  }

  /* Lexicographic byte order is big-endian numeric order. */
  constexpr size_t scalar_size = sizeof(unsigned long long);
  while (remaining >= scalar_size) {
    unsigned long long a;
    unsigned long long b;
    __builtin_memcpy(&a, l, sizeof(a));
    __builtin_memcpy(&b, r, sizeof(b));

    if (a != b) {
      return __builtin_bswapg(a) < __builtin_bswapg(b) ? -1 : 1;
    }

    l += scalar_size;
    r += scalar_size;
    remaining -= scalar_size;
  }

  for (size_t i = 0; i < remaining; i++) {
    if (l[i] != r[i]) {
      return l[i] < r[i] ? -1 : 1;
    }
  }

  return 0;
}
