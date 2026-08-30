#ifndef LIBUC_UNISTD_H
#define LIBUC_UNISTD_H

int pipe(int fds[2]);
int pipe2(int fds[2], int flags);
int close(int fd);

#endif
