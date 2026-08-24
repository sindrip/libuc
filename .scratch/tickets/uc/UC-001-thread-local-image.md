---
id: UC-001
title: Record the executable thread-local image
status: done
depends: []
---

## Goal

Turn `PT_TLS` into immutable process metadata. Do not allocate a block or
change the thread pointer yet.

## Spec

Parse the executable program headers once after auxv initialization. Record
the initialization bytes, initialized size, total size, and alignment. No
`PT_TLS` is a valid empty image; malformed or multiple entries fail startup.

## Files

- `libuc/src/thread_local/thread_local.{c,h}`
- `libuc/src/start.c`

## Acceptance

Both architectures build, and startup accepts executables both with and without
a `PT_TLS` program header.

## Outcome

Startup now reads `AT_PHDR`, `AT_PHENT`, and `AT_PHNUM` after auxv
initialization and records one immutable thread-local image descriptor. The
descriptor holds the initialization address, initialized size, total size, and
effective alignment; no `PT_TLS` produces the empty image with alignment one.

Missing or inconsistent auxv metadata, an invalid image size or alignment,
address arithmetic overflow, and multiple `PT_TLS` entries all fail startup.
This step allocates nothing and does not read or write a thread pointer.

The link contract now keeps the ELF and program headers in its first read-only
`PT_LOAD`. Before this ticket an empty `.rodata` let LLD begin the first mapping
at `.text`, leaving the address supplied in `AT_PHDR` unmapped.

Acceptance was checked on AArch64 and x86-64 under Linux. Each architecture's
TLS probe has exactly one `PT_TLS` with a four-byte initialization image,
eight-byte total image, and four-byte alignment; the companion executable has
no `PT_TLS`. All four executables return status zero, and both Meson builds pass
the project's `-Weverything -Werror` configuration with minimal UBSan enabled.
