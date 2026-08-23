#ifndef RT_CRASH_H
#define RT_CRASH_H

void rt_crash_install(void);

[[noreturn]] void rt_panic(const char *what, void *where);

#endif
