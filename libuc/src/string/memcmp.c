#include <stdint.h>

#include <string.h>

#include "memcmp_arch.h"

constexpr size_t word_width = sizeof(uint64_t);
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
  while (remaining >= word_width) {
    uint64_t av;
    uint64_t bv;
    __builtin_memcpy(&av, a, sizeof(av));
    __builtin_memcpy(&bv, b, sizeof(bv));

    if (av != bv) {
      return __builtin_bswap64(av) < __builtin_bswap64(bv) ? -1 : 1;
    }

    a += word_width;
    b += word_width;
    remaining -= word_width;
  }

  for (size_t i = 0; i < remaining; i++) {
    if (a[i] != b[i]) {
      return a[i] < b[i] ? -1 : 1;
    }
  }

  return 0;
}
