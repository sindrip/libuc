/*
 * Minimal formatting into a caller-owned buffer. Written for the crash
 * handler but deliberately not crash-specific (RT-007's note): every later
 * diagnostic reuses this instead of growing a printf.
 */
#ifndef RT_FMT_H
#define RT_FMT_H

/* An appender: p is the write cursor, end is one past the buffer. An append
 * that would overflow never writes out of bounds: strings truncate mid-way,
 * numbers refuse outright (see each contract) — a diagnostic path must never
 * fault while reporting a fault, and a short dump still names the signal. */
struct rt_fmt {
  char *p;
  char *end;
};

/* TODO(1): Append a NUL-terminated string. The NUL itself is never written —
 * the buffer is length-delimited by the cursor, and raw_write takes a count.
 */
void rt_fmt_str(struct rt_fmt *f, const char *s);

/* TODO(2): Append v as exactly 16 lowercase hex digits, zero-padded.
 *
 * Fixed width, not minimal: the dump prints 31 registers in columns, and
 * values that change width with their magnitude cannot be scanned. Padding
 * also renders "this register is zero" as visible zeros rather than a blank.
 * Four bits per digit, top nibble first — no division needed at all.
 *
 * All sixteen digits or none: a register printed at partial width reads as a
 * smaller value — a lie of distortion — where an absent field is a visible
 * gap. With fewer than sixteen bytes of room, nothing is written.
 */
void rt_fmt_hex(struct rt_fmt *f, unsigned long v);

/* TODO(3): Append v in decimal — the same backwards fill as main.c's
 * put_dec, into the appender instead of the console.
 *
 * All digits or none, the hex policy for a worse hazard: a truncated decimal
 * is a plausible smaller number with no tell at all, where a half-width hex
 * field at least violates its own format. The length needs no computing up
 * front — it falls out of the fill as a byproduct.
 */
void rt_fmt_dec(struct rt_fmt *f, unsigned long v);

#endif /* RT_FMT_H */
