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
ARG ARCH=arm64
COPY build/kernel.config /tmp/kernel.config
RUN make ARCH="${ARCH}" tinyconfig \
    && ./scripts/kconfig/merge_config.sh -m -O . .config \
        kernel/configs/kvm_guest.config /tmp/kernel.config \
    && make ARCH="${ARCH}" olddefconfig

# KERNEL_IMAGE rides along because the arches disagree on both the make
# target and its path: arm64 makes Image, x86_64 makes bzImage.
FROM kconfig AS kernel-build
ARG ARCH=arm64
ARG KERNEL_IMAGE=arch/arm64/boot/Image
RUN make ARCH="${ARCH}" -j"$(nproc)" "$(basename "${KERNEL_IMAGE}")" \
    && cp "${KERNEL_IMAGE}" /vmlinuz

# Pinned to the build platform: installing headers needs no cross toolchain,
# so both target platforms share one native run of everything above.
FROM --platform=$BUILDPLATFORM kernel-tree AS uapi-build
ARG TARGETARCH
RUN case "${TARGETARCH}" in arm64) arch=arm64 ;; amd64) arch=x86_64 ;; *) exit 1 ;; esac \
    && make ARCH="${arch}" headers

FROM scratch AS linux-src
COPY --from=linux-tarball /linux-*/ /

FROM scratch AS uapi
COPY --from=uapi-build /linux/usr/include /include

FROM scratch AS config
COPY --from=kconfig /linux/.config /kernel.config

# .config from kernel-build, not kconfig: the one that produced this vmlinuz.
FROM scratch AS kernel
COPY --from=kernel-build /vmlinuz /
COPY --from=kernel-build /linux/.config /kernel.config
COPY --from=kernel-build /linux/System.map /
