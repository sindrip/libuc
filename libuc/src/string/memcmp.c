#include <stdint.h>

#include <string.h>

typedef unsigned _BitInt(128) block128;
typedef unsigned char lane64 [[gnu::vector_size(64)]];

constexpr size_t block_width = sizeof(block128);
constexpr size_t wide_width = sizeof(lane64);
static_assert(block_width == 16);
static_assert(wide_width == 64);
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);

[[gnu::always_inline]]
static inline int compare_span(const unsigned char *a, const unsigned char *b,
                               size_t width) {
  block128 av = 0;
  block128 bv = 0;
  __builtin_memcpy(&av, a, width);
  __builtin_memcpy(&bv, b, width);

  if (av == bv) {
    return 0;
  }

  return __builtin_bswapg(av) < __builtin_bswapg(bv) ? -1 : 1;
}

[[gnu::always_inline]]
static inline int compare_pair(const unsigned char *a, const unsigned char *b,
                               size_t n, size_t width) {
  const int head = compare_span(a, b, width);
  if (head != 0) {
    return head;
  }
  return compare_span(a + n - width, b + n - width, width);
}

[[gnu::always_inline]]
static inline int compare_wide(const unsigned char *a, const unsigned char *b) {
  lane64 av;
  lane64 bv;
  __builtin_memcpy(&av, a, sizeof(av));
  __builtin_memcpy(&bv, b, sizeof(bv));

  // Each ISA lowers only some equality spellings to its native across-vector
  // test, and no spelling is native on both:
  //
  //   reduce_max(av ^ bv) == 0     aarch64: umaxv     x86-64: pmaxub funnel
  //   reduce_and(av == bv)         aarch64: funnel    x86-64: movemask
  //   reduce_or(av ^ bv) == 0      aarch64: funnel    x86-64: movemask
  //   reduce_min(av == bv) != 0    aarch64: funnel    x86-64: movemask
  if (__builtin_reduce_max(av ^ bv) == 0) {
    return 0;
  }

  for (size_t i = 0; i < wide_width - block_width; i += block_width) {
    const int result = compare_span(a + i, b + i, block_width);
    if (result != 0) {
      return result;
    }
  }
  return compare_span(a + wide_width - block_width,
                      b + wide_width - block_width, block_width);
}

int memcmp(const void *lhs, const void *rhs, size_t n) {
  const unsigned char *a = lhs;
  const unsigned char *b = rhs;

  if (n >= block_width) {
    const int head = compare_span(a, b, block_width);
    if (head != 0) {
      return head;
    }

    if (n <= 2 * block_width) {
      return compare_span(a + n - block_width, b + n - block_width,
                          block_width);
    }

    if (n <= 4 * block_width) {
      const int second =
          compare_span(a + block_width, b + block_width, block_width);
      if (second != 0) {
        return second;
      }
      return compare_pair(a + n - 2 * block_width, b + n - 2 * block_width,
                          2 * block_width, block_width);
    }

    // The head proved the first block equal, which licenses skipping up to
    // one block to align the loop; the final chunk overlaps as usual.
    const unsigned char *const last_a = a + n - wide_width;
    const unsigned char *const last_b = b + n - wide_width;
    const size_t skew =
        block_width - (size_t)((uintptr_t)a & (block_width - 1));
    a += skew;
    b += skew;

    while (a < last_a) {
      const int result = compare_wide(a, b);
      if (result != 0) {
        return result;
      }
      a += wide_width;
      b += wide_width;
    }

    return compare_wide(last_a, last_b);
  }

  if (n >= 8) {
    return compare_pair(a, b, n, 8);
  }

  if (n >= 4) {
    return compare_pair(a, b, n, 4);
  }

  if (n >= 2) {
    return compare_pair(a, b, n, 2);
  }

  if (n >= 1) {
    return compare_pair(a, b, n, 1);
  }

  return 0;
}
