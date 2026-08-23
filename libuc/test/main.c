#include <stddef.h>

/* Initializers must run before main, in priority order: numbered slots
 * first (101 before 202; 1..100 are reserved by the toolchain), then the
 * unprioritized one after every numbered slot. Each stage refuses to
 * advance unless the previous one already happened. */
static volatile int stage;

/* -Wglobal-constructors exists to flag initialization hiding before main;
 * proving that initialization runs is this file's entire purpose. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wglobal-constructors"

[[gnu::constructor(101)]] static void init_first(void) {
  stage = (stage == 0) ? 1 : -1;
}

[[gnu::constructor(202)]] static void init_second(void) {
  stage = (stage == 1) ? 2 : -1;
}

[[gnu::constructor]] static void init_last(void) {
  stage = (stage == 2) ? 3 : -1;
}

#pragma clang diagnostic pop

int main(int argc, char **argv, char **envp) {
  if (stage != 3) {
    return 125;
  }

  if (argc < 1 || argv == nullptr || argv[0] == nullptr || envp == nullptr) {
    return 127;
  }

  const size_t argument_count = (size_t)argc;
  if (argv[argument_count] != nullptr || envp != &argv[argument_count + 1]) {
    return 126;
  }

  return 0;
}
