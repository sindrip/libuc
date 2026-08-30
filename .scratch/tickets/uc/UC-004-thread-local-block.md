---
id: UC-004
title: Create an independent thread-local block
status: next
depends: [UC-001, UC-002]
---

## Staged implementation

UC-004-min establishes the runtime-owned block and TCB before compiler-visible
`_Thread_local` installation is introduced. It allocates the architecture-
correct mapping, copies and zero-fills the recorded image when present, and
initializes the two runtime TCB words:

```text
self pointer -> TCB itself
fiber pointer -> null
```

The block carries its raw mapping, mapping length, block address, and future
thread-pointer value. Creation does not install the thread pointer; that
belongs to UC-005. An executable without `PT_TLS` still receives a TCB-only
block, which is the intended bridge to fiber suspension before compiler TLS is
enabled.

The original acceptance remains outstanding: the staged probe covers distinct
live blocks, TCB initialization, image initialization, and destruction, but
does not yet install either block or exercise `_Thread_local` accesses.

## Goal

Make thread-local storage an owned object that can belong to a fiber.

## Spec

Add create and destroy operations. Creation yields an ABI-correct block: the
recorded image copied in, the zero-fill tail zero, the TCB initialized where
the architecture demands it, and the thread-pointer value computed — without
installing anything.

The TCB has two pointer-sized runtime words: its own address followed by the
fiber whose block this is. The self-pointer is required by x86-64 and retained
on aarch64 as the common libuc shape. The fiber pointer begins null; binding it
belongs to UC-006, once the block becomes part of a fiber. AArch64's ABI-fixed
16-byte TCB already has room for both words. Libuc's x86-64 TCB grows from 8 to
16 bytes above the thread pointer, which does not move the TLS block below it
or alter any compiled negative offset.

The current-fiber word is TCB metadata rather than an internal `_Thread_local`
variable. An executable with no `PT_TLS` must therefore still get a useful TCB
without libuc manufacturing a `PT_TLS` segment of its own.

The per-architecture placement seams in `src/arch/*/thread_local_arch.h`
already compute the geometry, measured against the toolchain; what remains is
the lifetime, and its shape is decided here against the fiber as the real
caller: create-owns-its-mapping versus a size query plus carve into memory
the fiber's own allocation provides. The trade-offs are recorded in
`../findings.md`.

`block_size` and `p_align` are whatever the executable declared. Every size
computation fails rather than wraps, and `p_align` may exceed the page size —
whichever shape lands must still place the block at its declared alignment.

## Files

- `src/thread_local/thread_local.{c,h}`
- `src/arch/*/thread_local_arch.h`
- `src/fiber/`

## Acceptance

Two simultaneously live blocks have distinct storage and identical initial
contents. Each thread pointer addresses a self-pointer followed by a null fiber
pointer, including when the executable has no `PT_TLS`; both blocks can be
destroyed.
