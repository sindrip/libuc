/*
 * Formatting bodies. Contracts are in fmt.h, next to the declarations.
 */

#include "fmt.h"

void rt_fmt_str(struct rt_fmt *f, const char *s) {
  while (f->p < f->end && *s != '\0') {
    *f->p++ = *s++;
  }
}

void rt_fmt_hex(struct rt_fmt *f, unsigned long v) {
  /* All or nothing: refuse outright unless all sixteen digits fit. Pointer
   * subtraction is signed (ptrdiff_t, which is what auto deduces), so a
   * broken p > end invariant goes negative and still refuses — fails
   * closed. This one guard proves every write below safe; there is
   * deliberately no per-write check. */
  auto remaining = f->end - f->p;
  if (remaining < 16) {
    return;
  }

  static constexpr char hexmap[] = "0123456789abcdef";

  /* Sixteen positions, top nibble first: the shift walks 60 down to 0 in
   * steps of 4, and must be SIGNED — an unsigned counter stepped below zero
   * wraps huge and its >= 0 test never fails. The & with 0b1111 is the
   * bounds proof for the hexmap index. Zero-padding is free: sixteen
   * unconditional iterations emit leading zeros with no special case. */
  for (int shift = 60; shift >= 0; shift -= 4) {
    unsigned long nibble = (v >> shift) & 0b1111;
    *f->p++ = hexmap[nibble];
  }
}

void rt_fmt_dec(struct rt_fmt *f, unsigned long v) {
  /* Digits emerge least-significant first, so fill a scratch buffer
   * backwards from its end; do-while so zero prints one digit instead of
   * none. Twenty bytes: the largest unsigned long is 18446744073709551615 —
   * twenty digits exactly. */
  char buf[20];
  char *p = buf + sizeof buf;

  do {
    *--p = (char)('0' + v % 10);
    v /= 10;
  } while (v);

  /* All or nothing, same policy as rt_fmt_hex — worse, even: a truncated
   * decimal is a plausible smaller number with no tell at all. The length is
   * a byproduct of the fill: how far p sits below the scratch's end. */
  auto len = buf + sizeof buf - p;
  if (f->end - f->p < len) {
    return;
  }

  /* Proven copy: the guard bounds the destination, the scratch's own end
   * bounds the source. No per-byte check, deliberately. */
  while (p < buf + sizeof buf) {
    *f->p++ = *p++;
  }
}
