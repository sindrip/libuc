# libuc

A fiber-native Linux libc built directly on io_uring.

Every potentially blocking kernel operation goes through the ring whenever the
kernel provides an opcode; the standard libc API hides fiber suspension from
the application.
