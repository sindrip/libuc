#ifndef RT_FMT_H
#define RT_FMT_H

struct rt_fmt {
  char *p;
  char *end;
};

void rt_fmt_str(struct rt_fmt *f, const char *s);

void rt_fmt_hex(struct rt_fmt *f, unsigned long v);

void rt_fmt_dec(struct rt_fmt *f, unsigned long v);

#endif
