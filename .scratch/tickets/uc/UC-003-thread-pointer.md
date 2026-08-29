---
id: UC-003
title: Install a thread-local block
status: todo
depends: [UC-002]
---

## Goal

Separate “make this block current” from creating the block.

## Spec

Expose one private install operation. AArch64 writes `tpidr_el0`; x86-64 writes
the FS base. Installation performs no allocation and owns no lifetime.

## Files

- `src/thread_local/thread_local.{c,h}`
- `src/arch/*/thread_local_arch.{c,h}`

## Acceptance

Alternating between two blocks alternates the observed value of the same
`_Thread_local` variable without copying data during installation.
