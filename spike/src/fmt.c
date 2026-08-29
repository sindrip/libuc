#include "fmt.h"

void rt_fmt_str(struct rt_fmt *f, const char *s) {
  while (f->p < f->end && *s != '\0') {
    *f->p++ = *s++;
  }
}

void rt_fmt_hex(struct rt_fmt *f, unsigned long v) {

  auto remaining = f->end - f->p;
  if (remaining < 16) {
    return;
  }

  static constexpr char hexmap[] = "0123456789abcdef";

  for (int shift = 60; shift >= 0; shift -= 4) {
    unsigned long nibble = (v >> shift) & 0b1111;
    *f->p++ = hexmap[nibble];
  }
}

void rt_fmt_dec(struct rt_fmt *f, unsigned long v) {

  char buf[20];
  char *p = buf + sizeof buf;

  do {
    *--p = (char)('0' + v % 10);
    v /= 10;
  } while (v);

  auto len = buf + sizeof buf - p;
  if (f->end - f->p < len) {
    return;
  }

  while (p < buf + sizeof buf) {
    *f->p++ = *p++;
  }
}
