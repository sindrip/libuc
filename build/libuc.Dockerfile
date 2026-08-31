# syntax=docker/dockerfile:1
# The CI build of libuc: the check and release meson runs from AGENTS.md,
# plus clang-tidy over the check build. nix realizes the toolchain from the
# same flake.lock the devshell uses, so CI's Clang/LLD are byte-for-byte the
# locally verified ones. Clang cross-compiles, so every platform builds on
# $BUILDPLATFORM; the case lines map docker's TARGETARCH spelling onto
# meson's cpu_family.
#
# The image leaves flakes disabled, and `nix store gc` in the build layer
# drops what only evaluation needed — the out-link keeps the toolchain
# closure rooted.
FROM --platform=$BUILDPLATFORM nixos/nix:2.35.2@sha256:7a007c766426c1877758ddc5cb87a965ac131fc78c582ce0083d922d51ae945c AS toolchain
RUN echo 'experimental-features = nix-command flakes' >> /etc/nix/nix.conf
WORKDIR /flake
COPY flake.nix flake.lock ./
RUN nix build .#toolchain --out-link /toolchain && nix store gc
ENV PATH=/toolchain/bin:$PATH

# `uapi` is a bake named context (target:uapi), resolved per platform: the
# headers arrive from kernel.Dockerfile's export stage, never from out/,
# and land on the linux_<TARGETARCH> path the cross files read.
FROM toolchain AS build
ARG TARGETARCH
WORKDIR /src
COPY . .
COPY --from=uapi /include out/uapi/linux_${TARGETARCH}/include
RUN case "${TARGETARCH}" in arm64) cpu=aarch64 ;; amd64) cpu=x86_64 ;; *) exit 1 ;; esac \
    && meson setup .cache/check --cross-file "cross/${cpu}.ini" -Dtests=true -Dubsan=enabled \
    && meson compile -C .cache/check \
    && meson test -C .cache/check --print-errorlogs \
    && ninja -C .cache/check clang-tidy
RUN case "${TARGETARCH}" in arm64) cpu=aarch64 ;; amd64) cpu=x86_64 ;; *) exit 1 ;; esac \
    && meson setup .cache/release --cross-file "cross/${cpu}.ini" -Dtests=false -Dubsan=disabled \
    && meson compile -C .cache/release \
    && meson install -C .cache/release --destdir /stage \
    && tar -C /stage/usr -czf "/libuc-${cpu}.tar.gz" LICENSE include lib

FROM scratch AS libuc
COPY --from=build /libuc-*.tar.gz /
