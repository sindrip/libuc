#include <string.h>

#include "memcmp_arch.h"

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);
typedef unsigned long long scalar;
constexpr size_t scalar_size = sizeof(scalar);

/* How far a scan runs past a difference before noticing. */
constexpr size_t memcmp_block_size = 64;
static_assert(memcmp_block_size % sizeof(memcmp_acc) == 0);

/* A fixed-trip reduction with no early exit: the shape the loop
   vectorizer lowers to full vector width. A mismatch test inside the
   loop would devectorize it. */
[[gnu::always_inline]]
static inline bool memcmp_block_equal(const unsigned char *l,
                                      const unsigned char *r) {
  memcmp_acc acc = 0;
  for (size_t i = 0; i < memcmp_block_size; i += sizeof(memcmp_acc)) {
    memcmp_acc a;
    memcmp_acc b;
    __builtin_memcpy(&a, l + i, sizeof(a));
    __builtin_memcpy(&b, r + i, sizeof(b));
    acc = memcmp_accumulate(acc, (memcmp_acc)(a ^ b));
  }
  return acc == 0;
}

int memcmp(const void *lhs, const void *rhs, size_t n) {
  const unsigned char *l = lhs;
  const unsigned char *r = rhs;
  size_t remaining = n;

  /* The break skips the decrement; the difference is in the first block. */
  while (remaining >= memcmp_block_size) {
    if (!memcmp_block_equal(l, r)) {
      break;
    }
    l += memcmp_block_size;
    r += memcmp_block_size;
    remaining -= memcmp_block_size;
  }

  /* Lexicographic byte order is big-endian numeric order. */
  while (remaining >= scalar_size) {
    scalar a;
    scalar b;
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
