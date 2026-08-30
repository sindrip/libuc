---
id: UC-011
title: Pool stacks and decide block ownership
status: todo
depends: [UC-009]
---

## Goal

Make fiber spawn from a per-scheduler pool cheap, and settle
create-versus-carve with measurements instead of projections.

## Spec

A per-scheduler stack pool, per `../../stacks.md`. Against it, the deferred
UC-004 decision comes due: create-owns (two mappings per fiber, pool cannot
absorb the TLS mmap) versus geometry-and-carve (one mapping, recycle is
memset+memcpy). The constraints that kept the swap an internal refactor are
recorded in `../findings.md`; whichever shape wins, `block_create`'s
alignment slack is trimmed or deleted with it.

## Acceptance

A probe reports spawn and recycle syscall counts for both shapes on the
console; the chosen shape lands with the pool, and the numbers plus the
decision are recorded in `../findings.md`.
