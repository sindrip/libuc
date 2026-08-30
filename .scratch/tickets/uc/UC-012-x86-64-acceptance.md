---
id: UC-012
title: Restore x86-64 behavioral acceptance
status: todo
depends: []
---

## Goal

Run the fiber and thread-local acceptance on real x86-64, retiring the
compile-and-link-only tier.

## Spec

Blocked on an environment, not a ticket: a kernel-advertised-FSGSBASE
x86-64 machine or guest. Rosetta hangs on the startup install and qemu-user
does not advertise the hwcap, so no emulator on this host can serve. This
is the first execution of `wrfsbase`/`rdfsbase` anywhere.

## Acceptance

Every probe exits with its aarch64 status on x86-64: thread-local-install
exits 0 rather than 125, fiber and fiber-thread-local exit 0, exit-status
exits 42.
