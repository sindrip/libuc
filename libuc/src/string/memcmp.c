#include <stdint.h>

#include <string.h>

typedef unsigned _BitInt(128) block128 [[gnu::aligned(1), gnu::may_alias]];
typedef unsigned _BitInt(64) block64 [[gnu::aligned(1), gnu::may_alias]];
typedef unsigned _BitInt(32) block32 [[gnu::aligned(1), gnu::may_alias]];
typedef unsigned _BitInt(16) block16 [[gnu::aligned(1), gnu::may_alias]];
typedef unsigned char lane64
    [[gnu::vector_size(64), gnu::aligned(1), gnu::may_alias]];

constexpr size_t block_width = sizeof(block128);
constexpr size_t wide_width = sizeof(lane64);
static_assert(block_width == 16);
static_assert(wide_width == 64);
static_assert(alignof(block128) == 1);
static_assert(alignof(lane64) == 1);
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);

[[gnu::always_inline]]
static inline int compare_block(const unsigned char *a,
                                const unsigned char *b) {
  const block128 av = *(const block128 *)a;
  const block128 bv = *(const block128 *)b;

  if (av == bv) {
    return 0;
  }

  return __builtin_bswapg(av) < __builtin_bswapg(bv) ? -1 : 1;
}

[[gnu::always_inline]]
static inline int compare_wide(const unsigned char *a, const unsigned char *b) {
  const lane64 av = *(const lane64 *)a;
  const lane64 bv = *(const lane64 *)b;

  if (__builtin_reduce_max(av ^ bv) == 0) {
    return 0;
  }

  for (size_t i = 0; i < wide_width - block_width; i += block_width) {
    const int result = compare_block(a + i, b + i);
    if (result != 0) {
      return result;
    }
  }
  return compare_block(a + wide_width - block_width,
                       b + wide_width - block_width);
}

int memcmp(const void *lhs, const void *rhs, size_t n) {
  const unsigned char *a = lhs;
  const unsigned char *b = rhs;

  if (n >= block_width) {
    const int head = compare_block(a, b);
    if (head != 0) {
      return head;
    }

    if (n <= 2 * block_width) {
      return compare_block(a + n - block_width, b + n - block_width);
    }

    if (n <= 4 * block_width) {
      const int second = compare_block(a + block_width, b + block_width);
      if (second != 0) {
        return second;
      }
      const int third =
          compare_block(a + n - 2 * block_width, b + n - 2 * block_width);
      if (third != 0) {
        return third;
      }
      return compare_block(a + n - block_width, b + n - block_width);
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

  if (n >= sizeof(block64)) {
    const block64 head_a = *(const block64 *)a;
    const block64 head_b = *(const block64 *)b;
    if (head_a != head_b) {
      return __builtin_bswapg(head_a) < __builtin_bswapg(head_b) ? -1 : 1;
    }
    const block64 tail_a = *(const block64 *)(a + n - sizeof(block64));
    const block64 tail_b = *(const block64 *)(b + n - sizeof(block64));
    if (tail_a != tail_b) {
      return __builtin_bswapg(tail_a) < __builtin_bswapg(tail_b) ? -1 : 1;
    }
    return 0;
  }

  if (n >= sizeof(block32)) {
    const block32 head_a = *(const block32 *)a;
    const block32 head_b = *(const block32 *)b;
    if (head_a != head_b) {
      return __builtin_bswapg(head_a) < __builtin_bswapg(head_b) ? -1 : 1;
    }
    const block32 tail_a = *(const block32 *)(a + n - sizeof(block32));
    const block32 tail_b = *(const block32 *)(b + n - sizeof(block32));
    if (tail_a != tail_b) {
      return __builtin_bswapg(tail_a) < __builtin_bswapg(tail_b) ? -1 : 1;
    }
    return 0;
  }

  if (n >= sizeof(block16)) {
    const block16 head_a = *(const block16 *)a;
    const block16 head_b = *(const block16 *)b;
    if (head_a != head_b) {
      return __builtin_bswapg(head_a) < __builtin_bswapg(head_b) ? -1 : 1;
    }
    const block16 tail_a = *(const block16 *)(a + n - sizeof(block16));
    const block16 tail_b = *(const block16 *)(b + n - sizeof(block16));
    if (tail_a != tail_b) {
      return __builtin_bswapg(tail_a) < __builtin_bswapg(tail_b) ? -1 : 1;
    }
    return 0;
  }

  if (n == 0) {
    return 0;
  }

  if (a[0] != b[0]) {
    return a[0] < b[0] ? -1 : 1;
  }

  return 0;
}
