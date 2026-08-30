---
id: UC-005
title: Install a thread-local block
status: done
depends: [UC-004]
---

## Goal

Separate “make this block current” from creating the block.

## Spec

Expose one private install operation. AArch64 writes `tpidr_el0`; x86-64
writes the FS base. Installation performs no allocation and owns no lifetime.

## Files

- `src/thread_local/thread_local.{c,h}`
- `src/arch/*/thread_local_arch.h`

## Acceptance

Alternating between two blocks alternates the observed value of the same
`_Thread_local` variable without copying data during installation.

2026-08-30: met by `test/thread_local_install.c` — exit 0 in the aarch64
container and VM. Availability is a separate init-time question
(`__libuc_thread_local_install_available`); install itself is the bare
register write, so the normal case pays zero checks. x86-64 exits 125:
neither emulator advertises `HWCAP2_FSGSBASE`, so the fail-closed
availability probe is what is verified there; the `wrfsbase` path itself
is UC-012.
