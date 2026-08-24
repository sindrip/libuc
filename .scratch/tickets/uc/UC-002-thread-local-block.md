---
id: UC-002
title: Create an independent thread-local block
status: todo
depends: [UC-001]
---

## Goal

Make thread-local storage an owned object that can belong to a fiber.

## Spec

Add create and destroy operations. Creation allocates an ABI-correct block,
copies the recorded initialization image, and leaves the zero-fill tail zero.
It returns the block and its thread-pointer value without installing it.

Keep architecture-specific placement behind `thread_local_arch.h`; keep
allocation and lifetime generic.

## Files

- `libuc/src/thread_local/thread_local.{c,h}`
- `libuc/src/arch/*/thread_local_arch.{c,h}`

## Acceptance

Two simultaneously live blocks have distinct storage and identical initial
contents, and both mappings can be destroyed.
