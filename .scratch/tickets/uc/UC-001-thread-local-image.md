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

- `src/thread_local/thread_local.{c,h}`
- `src/start.c`

## Acceptance

Both architectures build, and startup accepts executables both with and without
a `PT_TLS` program header.

## Outcome

Startup now reads `AT_PHDR`, `AT_PHENT`, and `AT_PHNUM` after auxv
initialization and records one immutable thread-local image descriptor. The
layout holds the initialization image, image size, future thread-local block
size, and effective alignment; no `PT_TLS` produces the empty layout with
alignment one.

The layout query memoizes successful decoding and returns the same immutable
descriptor thereafter. A failed decode is deliberately not cached: startup
immediately returns status 127 on the first `nullptr`, so no later code can
observe or retry that failure in the current contract.

Missing or inconsistent auxv metadata, an invalid image size or alignment,
address arithmetic overflow, and multiple `PT_TLS` entries all fail startup.
This step allocates nothing and does not read or write a thread pointer.

`PT_TLS` is deliberately the metadata ABI rather than symbols exported by
`libuc.ld`: the eventual installed CRT must also work on link lines libuc does
not own. Validation is limited to values libuc consumes and arithmetic it will
perform. In particular, it does not inspect `p_offset`; file layout coherence
belongs to the linker and kernel once the executable has been mapped.

The link contract now keeps the ELF and program headers in its first read-only
`PT_LOAD`. Before this ticket an empty `.rodata` let LLD begin the first mapping
at `.text`, leaving the address supplied in `AT_PHDR` unmapped. LLD continues to
synthesize the optional `PT_TLS`; declaring it through `PHDRS` would also add an
empty segment to the no-TLS executable and erase that case from the contract.

Acceptance was checked on AArch64 and x86-64 under Linux. Each architecture's
TLS probe has exactly one `PT_TLS` with a four-byte initialization image,
eight-byte total image, and four-byte alignment; the companion executable has
no `PT_TLS`. A native Meson test checks that inventory with `llvm-readelf`, while
both executables separately check the decoded runtime image. All four
executables return status zero, and both Meson builds pass the project's
`-Weverything -Werror` configuration with minimal UBSan enabled.
