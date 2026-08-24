#include <string.h>

#include "memcmp_arch.h"

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);

int memcmp(const void *lhs, const void *rhs, size_t n) {
  const unsigned char *a = lhs;
  const unsigned char *b = rhs;
  size_t remaining = n;

  /* The break skips the decrement; the difference is in the first window. */
  while (remaining >= memcmp_arch_block_size) {
    if (!memcmp_arch_equal(a, b)) {
      break;
    }
    a += memcmp_arch_block_size;
    b += memcmp_arch_block_size;
    remaining -= memcmp_arch_block_size;
  }

  /* Lexicographic byte order is big-endian numeric order. */
  constexpr size_t scalar_size = sizeof(unsigned long long);
  while (remaining >= scalar_size) {
    unsigned long long av;
    unsigned long long bv;
    __builtin_memcpy(&av, a, sizeof(av));
    __builtin_memcpy(&bv, b, sizeof(bv));

    if (av != bv) {
      return __builtin_bswapg(av) < __builtin_bswapg(bv) ? -1 : 1;
    }

    a += scalar_size;
    b += scalar_size;
    remaining -= scalar_size;
  }

  for (size_t i = 0; i < remaining; i++) {
    if (a[i] != b[i]) {
      return a[i] < b[i] ? -1 : 1;
    }
  }

  return 0;
}
