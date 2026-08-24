#include <string.h>

#include "memcmp_arch.h"

constexpr size_t scalar_width = sizeof(unsigned long long);
constexpr size_t wide_width = memcmp_arch_width;
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);

int memcmp(const void *lhs, const void *rhs, size_t n) {
  const unsigned char *a = lhs;
  const unsigned char *b = rhs;
  size_t remaining = n;

  /* The break skips the decrement; the difference is in the first window. */
  while (remaining >= wide_width) {
    if (!memcmp_arch_equal(a, b)) {
      break;
    }
    a += wide_width;
    b += wide_width;
    remaining -= wide_width;
  }

  /* Lexicographic byte order is big-endian numeric order. */
  while (remaining >= scalar_width) {
    unsigned long long av;
    unsigned long long bv;
    __builtin_memcpy(&av, a, sizeof(av));
    __builtin_memcpy(&bv, b, sizeof(bv));

    if (av != bv) {
      return __builtin_bswapg(av) < __builtin_bswapg(bv) ? -1 : 1;
    }

    a += scalar_width;
    b += scalar_width;
    remaining -= scalar_width;
  }

  for (size_t i = 0; i < remaining; i++) {
    if (a[i] != b[i]) {
      return a[i] < b[i] ? -1 : 1;
    }
  }

  return 0;
}
