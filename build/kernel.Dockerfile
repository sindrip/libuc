# syntax=docker/dockerfile:1
FROM scratch AS linux-tarball
ARG KERNEL_VERSION
ARG KERNEL_SHA256
ADD --checksum=sha256:${KERNEL_SHA256} --unpack \
    https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-${KERNEL_VERSION}.tar.xz /

FROM alpine@sha256:28bd5fe8b56d1bd048e5babf5b10710ebe0bae67db86916198a6eec434943f8b AS toolchain
RUN apk add --no-cache gcc make musl-dev bc flex bison elfutils-dev perl \
    python3 linux-headers openssl-dev pahole

FROM toolchain AS kernel-tree
COPY --from=linux-tarball /linux-*/ /linux/
WORKDIR /linux

FROM kernel-tree AS kconfig
COPY build/kernel.config /tmp/kernel.config
RUN make ARCH=arm64 tinyconfig \
    && ./scripts/kconfig/merge_config.sh -m -O . .config \
        kernel/configs/kvm_guest.config /tmp/kernel.config \
    && make ARCH=arm64 olddefconfig

FROM kconfig AS kernel-build
RUN make ARCH=arm64 -j"$(nproc)" Image && cp arch/arm64/boot/Image /vmlinuz

FROM scratch AS linux-src
COPY --from=linux-tarball /linux-*/ /

FROM scratch AS config
COPY --from=kconfig /linux/.config /kernel.config

# .config from kernel-build, not kconfig: the one that produced this vmlinuz.
FROM scratch AS kernel
COPY --from=kernel-build /vmlinuz /
COPY --from=kernel-build /linux/.config /kernel.config
COPY --from=kernel-build /linux/System.map /
