/* Roundtrips a 64 KiB buffer through vendored lz4. Exit status is the
 * report: 23 on success, a distinct failure code per stage. */

#include <string.h>

#include <lz4.h>

static char source[65536];
static char compressed[LZ4_COMPRESSBOUND(65536)];
static char decoded[65536];

int main([[maybe_unused]] int argc, [[maybe_unused]] char **argv,
         [[maybe_unused]] char **envp) {
  for (size_t i = 0; i < sizeof source; i++) {
    const unsigned char base = (unsigned char)"abcdefgh"[i & 7];
    source[i] = (char)(base + (unsigned char)(i >> 11));
  }

  const int packed =
      LZ4_compress_default(source, compressed, (int)sizeof source,
                           (int)sizeof compressed);
  if (packed <= 0) {
    return 1;
  }
  if (packed >= (int)sizeof source) {
    return 2;
  }

  const int unpacked =
      LZ4_decompress_safe(compressed, decoded, packed, (int)sizeof decoded);
  if (unpacked != (int)sizeof source) {
    return 3;
  }
  if (memcmp(source, decoded, sizeof source) != 0) {
    return 4;
  }

  return 23;
}
