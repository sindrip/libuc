#include <string.h>

typedef unsigned _BitInt(128) block128 [[gnu::aligned(1), gnu::may_alias]];

constexpr size_t block_width = sizeof(block128);
static_assert(block_width == 16);
static_assert(alignof(block128) == 1);
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);

[[gnu::always_inline]]
static inline int compare_block(const unsigned char *a,
                                const unsigned char *b) {
  const block128 differences = *(const block128 *)a ^ *(const block128 *)b;

  if (differences == 0) {
    return 0;
  }

  const size_t byte = (size_t)__builtin_ctzg(differences) / 8;
  return a[byte] < b[byte] ? -1 : 1;
}

int memcmp(const void *lhs, const void *rhs, size_t n) {
  const unsigned char *a = lhs;
  const unsigned char *b = rhs;

  if (n < block_width) {
    for (size_t i = 0; i < n; i++) {
      if (a[i] != b[i]) {
        return a[i] < b[i] ? -1 : 1;
      }
    }
    return 0;
  }

  // The final block; when n is not a block multiple it overlaps bytes the
  // loop will have proved equal, and equal bytes contribute no difference.
  const unsigned char *const last_a = a + n - block_width;
  const unsigned char *const last_b = b + n - block_width;

  while (a < last_a) {
    const int result = compare_block(a, b);
    if (result != 0) {
      return result;
    }
    a += block_width;
    b += block_width;
  }

  return compare_block(last_a, last_b);
}
