# syntax=docker/dockerfile:1
# The CI build of libuc: the check and release meson runs from AGENTS.md,
# in the same digest-pinned alpine as kernel.Dockerfile's toolchain stage.
# Clang cross-compiles, so every platform builds on $BUILDPLATFORM; the case
# lines map docker's TARGETARCH spelling onto meson's cpu_family.
FROM --platform=$BUILDPLATFORM alpine@sha256:28bd5fe8b56d1bd048e5babf5b10710ebe0bae67db86916198a6eec434943f8b AS toolchain
RUN apk add --no-cache clang lld llvm meson samurai

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
    && meson test -C .cache/check --print-errorlogs
RUN case "${TARGETARCH}" in arm64) cpu=aarch64 ;; amd64) cpu=x86_64 ;; *) exit 1 ;; esac \
    && meson setup .cache/release --cross-file "cross/${cpu}.ini" -Dtests=false -Dubsan=disabled \
    && meson compile -C .cache/release \
    && meson install -C .cache/release --destdir /stage \
    && tar -C /stage/usr -czf "/libuc-${cpu}.tar.gz" include lib

FROM scratch AS libuc
COPY --from=build /libuc-*.tar.gz /
