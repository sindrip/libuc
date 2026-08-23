/*
 * The operations a fiber can ask its scheduler to perform.
 *
 * Descriptions, not machinery. Each function below builds an SQE and hands it
 * to rt_fiber_await_io (fiber.h); none of them touches the ring, a context, a
 * scheduler, or the current fiber. That is why the lifetime argument for the
 * pointers they carry is stated once at rt_fiber_await_io rather than nine
 * times here, and why an operation cannot stage a request and forget to wait —
 * staging is not something it can do.
 *
 * Every result is the CQE's res, returned as-is: a new fd, a byte count, or
 * zero on success, and -errno on failure. No operation can fail to be
 * *issued*. A fiber always has somewhere to put a request, so a full SQ is
 * never its problem — the scheduler holds the request and stages it next
 * turn, because submission backpressure belongs to the only thing that can
 * resolve it.
 *
 * These names carry no runtime prefix on purpose. They are libuc's <unistd.h>
 * and <sys/socket.h> in embryo, and will eventually be spelled write, recv,
 * send and close; nothing about them should have to be renamed on the way.
 *
 * raw_write is forbidden in fiber bodies; failure paths under the purity
 * registry's charter are the only exception.
 */
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

#endif /* RT_IO_H */
