# syntax=docker/dockerfile:1
FROM scratch AS linux-tarball
ARG KERNEL_VERSION
ARG KERNEL_SHA256
ADD --checksum=sha256:${KERNEL_SHA256} --unpack \
    https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-${KERNEL_VERSION}.tar.xz /

FROM alpine@sha256:28bd5fe8b56d1bd048e5babf5b10710ebe0bae67db86916198a6eec434943f8b AS base

FROM base AS toolchain
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

FROM kernel-tree AS uapi-build
RUN make ARCH=arm64 headers

FROM kernel-tree AS uapi-x86_64-build
RUN make ARCH=x86_64 headers

FROM base AS runtime-toolchain
RUN apk add --no-cache clang lld clang-extra-tools make cpio gzip
COPY --from=uapi-build /linux/usr/include /uapi/include

FROM runtime-toolchain AS runtime-build
COPY src /src
RUN clang --target=aarch64-unknown-linux-gnu -std=c23 \
        -ffreestanding -nostdlibinc -isystem /uapi/include -I/src -I/src/arch/aarch64 \
        -nostdlib -nostartfiles -static -fuse-ld=lld \
        -fno-stack-protector -fno-omit-frame-pointer \
        -Wall -Wextra -pedantic -Wsign-conversion -Werror -g -O1 \
        -fsanitize=undefined -fsanitize-minimal-runtime -fno-sanitize-recover=all \
        -fno-sanitize-link-runtime \
        -o /init /src/arch/aarch64/start.S /src/arch/aarch64/context.c \
        /src/ring.c /src/auxv.c /src/fiber.c /src/io.c /src/scheduler.c /src/fmt.c /src/crash.c /src/ubsan.c /src/main.c \
    && mkdir -p /rootfs \
    && cp /init /rootfs/init \
    && cd /rootfs \
    && find . | cpio -o -H newc | gzip -9 > /initramfs.cpio.gz

FROM scratch AS linux-src
COPY --from=linux-tarball /linux-*/ /

FROM scratch AS uapi
COPY --from=uapi-build /linux/usr/include /include

FROM scratch AS uapi-x86_64
COPY --from=uapi-x86_64-build /linux/usr/include /include

FROM scratch AS config
COPY --from=kconfig /linux/.config /kernel.config

# .config from kernel-build, not kconfig: the one that produced this vmlinuz.
FROM scratch AS kernel
COPY --from=kernel-build /vmlinuz /
COPY --from=kernel-build /linux/.config /kernel.config
COPY --from=kernel-build /linux/System.map /
COPY --from=runtime-build /initramfs.cpio.gz /
COPY --from=runtime-build /init /rt.elf
