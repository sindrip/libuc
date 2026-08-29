---
id: UC-004
title: Create an independent thread-local block
status: todo
depends: [UC-001, UC-002]
---

## Goal

Make thread-local storage an owned object that can belong to a fiber.

## Spec

Add create and destroy operations. Creation yields an ABI-correct block: the
recorded image copied in, the zero-fill tail zero, the TCB initialized where
the architecture demands it (x86-64's self-pointer), and the thread-pointer
value computed — without installing anything.

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
contents, and both can be destroyed.
