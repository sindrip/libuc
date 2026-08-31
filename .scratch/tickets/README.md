# Tickets

This directory is the active libuc queue plus recent completed tickets whose
acceptance or public ABI detail is still useful during current work. Git history
is the archive; completed construction tickets are not retained indefinitely as
a second architecture manual.

## Active order

| id | waits on | outcome |
|---|---|---|
| UC-011 | nothing | scheduler-owned stack and TLS-block recycling |
| UC-019 | nothing | direct completion-loss detection for the current ring |
| UC-020 | UC-019 | operation records and the common pull-iterator engine |
| UC-017 | UC-020 | automatic iterator cancellation, zombies, and reclamation |
| UC-021 | UC-017, UC-018, UC-020 | typed accepted-connection iterator |
| UC-022 | UC-017, UC-018, UC-020, UC-021 | typed borrowed receive iterator |
| UC-023 | UC-017, UC-018, UC-020, UC-021 | one-result zero-copy send lifetime |

UC-012 is environment-blocked rather than implementation work: x86-64
behavioral acceptance needs an FSGSBASE-capable machine or guest.

The intended implementation order remains UC-011, UC-019, UC-020, UC-017,
UC-021, then UC-022 and UC-023. UC-019 is independent and may land before
UC-011 if ring correctness is being worked on directly.

## Retained completed records

- UC-013: the current fiber-await reactor and its single-CQE contract;
- UC-014: public descriptor I/O, count conversion, and per-fiber `errno`;
- UC-015: ready-generation fairness;
- UC-018: socket ABI authorship and the single-shot connection lifecycle.

## Retired history

UC-000 through UC-010 established startup, TLS, fibers, the ring, and scheduler
zero. Their current contracts are consolidated in `../plan.md`,
`../scheduler.md`, `../libuc.md`, `findings.md`, AGENTS.md, and the probes, so
their ticket files were removed during the 2026-08-31 grooming pass.

UC-016 was removed as superseded, not completed. Its operation-table experiment
was backed out in `cd95c0e`; durable kernel findings moved to `findings.md`,
completion loss became UC-019, repeated delivery became the iterator work in
UC-020 through UC-022, and zero-copy send lifetime became UC-023. Operation
records unify completion routing; typed iterators unify repeated value delivery.
Kernel multishot is not an API contract.
