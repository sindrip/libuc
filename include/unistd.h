#ifndef LIBUC_UNISTD_H
#define LIBUC_UNISTD_H

#include <stddef.h>

#include <sys/types.h>

int pipe(int fds[2]);
int pipe2(int fds[2], int flags);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);

#endif
