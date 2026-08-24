#include <string.h>

#include "memcmp_arch.h"

typedef unsigned long long word;

constexpr size_t word_width = sizeof(word);
constexpr size_t wide_width = memcmp_arch_width;
static_assert(word_width == 8);
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);

int memcmp(const void *lhs, const void *rhs, size_t n) {
  const unsigned char *a = lhs;
  const unsigned char *b = rhs;

  /* Three loops, each finer than the one before. This one advances a whole
   * window at a time and stops at the first window that differs, without
   * decrementing n for it, so the remaining count still covers the bytes
   * that differ and the loops below are certain to reach them. Decrementing
   * n there would step past the difference. */
  while (n >= wide_width) {
    if (!memcmp_arch_equal(a, b)) {
      break;
    }
    a += wide_width;
    b += wide_width;
    n -= wide_width;
  }

  /* Lexicographic order on bytes is numeric order on their big-endian
   * reading, so reversing the bytes of each word makes an ordinary integer
   * comparison give the answer memcmp wants. */
  while (n >= word_width) {
    word av;
    word bv;
    __builtin_memcpy(&av, a, sizeof(av));
    __builtin_memcpy(&bv, b, sizeof(bv));

    if (av != bv) {
      return __builtin_bswap64(av) < __builtin_bswap64(bv) ? -1 : 1;
    }

    a += word_width;
    b += word_width;
    n -= word_width;
  }

  for (size_t i = 0; i < n; i++) {
    if (a[i] != b[i]) {
      return a[i] < b[i] ? -1 : 1;
    }
  }

  return 0;
}
