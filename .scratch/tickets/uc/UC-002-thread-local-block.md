---
id: UC-002
title: Create an independent thread-local block
status: next
depends: [UC-001]
---

## Goal

Make thread-local storage an owned object that can belong to a fiber.

## Spec

Add create and destroy operations. Creation allocates an ABI-correct block,
copies the recorded initialization image, and leaves the zero-fill tail zero.
It returns the block and its thread-pointer value without installing it.

`block_size` is whatever the executable’s `PT_TLS` declared. UC-001 records it
without bounding it — its rule is to guard only the arithmetic it performs
itself — so every size computation here runs on input the executable controls.
Both placement variants reduce to `round_up(x, alignment) + y`:

| variant | round | add |
|---|---|---|
| I (AArch64, block above TP) | `round_up(tcb_size, alignment)` | `+ block_size` |
| II (x86-64, block below TP) | `round_up(block_size, alignment)` | `+ tcb_size` |

Both steps overflow on a malformed `p_memsz`, as does rounding the total up to
the mapping length. Creation fails rather than wraps. `<stdckdint.h>` is the
permitted freestanding header for this and compiles to the carry flag; the
round-up is `ckd_add(&raised, value, alignment - 1)` followed by masking, which
is exact at `alignment == 1` and never false-trips on a legal huge `p_align`.

`p_align` may legally exceed the page size — `alignas(65536)` on a
thread-local is enough — while `mmap` guarantees only page alignment.
Exceeding it therefore needs over-allocate-and-trim, not merely a larger
rounded length.

Keep architecture-specific placement behind `thread_local_arch.h`; keep
allocation and lifetime generic.

## Files

- `src/thread_local/thread_local.{c,h}`
- `src/arch/*/thread_local_arch.{c,h}`

## Acceptance

Two simultaneously live blocks have distinct storage and identical initial
contents, and both mappings can be destroyed.
