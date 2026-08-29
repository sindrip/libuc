#ifndef RT_IO_H
#define RT_IO_H

[[nodiscard]] int rt_nop(void);
[[nodiscard]] int rt_write(int fd, const void *buf, unsigned len);
[[nodiscard]] int rt_socket(int domain, int type, int protocol);
[[nodiscard]] int rt_bind(int fd, const void *addr, unsigned addr_len);
[[nodiscard]] int rt_listen(int fd, int backlog);
[[nodiscard]] int rt_accept(int fd);
[[nodiscard]] int rt_recv(int fd, void *buf, unsigned len);
[[nodiscard]] int rt_send(int fd, const void *buf, unsigned len);
[[nodiscard]] int rt_close(int fd);

#endif
