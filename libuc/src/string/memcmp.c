#include <stdint.h>

#include <string.h>

int memcmp(const void *lhs, const void *rhs, size_t n) {
  const unsigned char *a = lhs;
  const unsigned char *b = rhs;

  typedef uint64_t word_vector
      __attribute__((vector_size(16), aligned(1), may_alias));
  constexpr size_t vector_width = sizeof(word_vector);
  static_assert(sizeof(uint64_t) * 2 == vector_width);
  static_assert(alignof(word_vector) == 1);

  size_t i = 0;
  while (n - i >= vector_width) {
    const word_vector left = *(const word_vector *)(a + i);
    const word_vector right = *(const word_vector *)(b + i);
    const word_vector differences = left ^ right;
    const uint64_t low_difference = differences[0];
    const uint64_t high_difference = differences[1];
    if ((low_difference | high_difference) != 0) {
      uint64_t difference = low_difference;
      size_t byte = 0;
      if (low_difference == 0) {
        byte = sizeof(uint64_t);
        difference = high_difference;
      }
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      byte += (size_t)__builtin_ctzll((unsigned long long)difference) / 8;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      byte += (size_t)__builtin_clzll((unsigned long long)difference) / 8;
#else
#error "unsupported byte order"
#endif
      return a[i + byte] < b[i + byte] ? -1 : 1;
    }
    i += vector_width;
  }

  for (; i < n; i++) {
    if (a[i] != b[i]) {
      return a[i] < b[i] ? -1 : 1;
    }
  }
  return 0;
}
