#ifndef LIBUC_SYS_SOCKET_H
#define LIBUC_SYS_SOCKET_H

#include <linux/socket.h>

typedef __kernel_sa_family_t sa_family_t;
typedef unsigned int socklen_t;

struct sockaddr {
  sa_family_t sa_family;
  char sa_data[14];
};

/* Not in uapi: the names are POSIX's, the values are the kernel's. */
enum : int { AF_INET = 2 };
#define AF_INET AF_INET

enum : int { SOCK_STREAM = 1 };
#define SOCK_STREAM SOCK_STREAM

int socket(int domain, int type, int protocol);
int bind(int fd, const struct sockaddr *addr, socklen_t len);
int listen(int fd, int backlog);
int accept(int fd, struct sockaddr *addr, socklen_t *len);
int connect(int fd, const struct sockaddr *addr, socklen_t len);

#endif
