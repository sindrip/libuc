---
id: UC-001
title: Record the executable thread-local image
status: next
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
