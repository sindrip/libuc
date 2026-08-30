# Tickets

The `UC-*` queue under [`uc/`](uc/) established the fiber and its C
thread-local identity, then made the initial Linux task a single scheduler
running the root fiber through its own ring. Numbers follow dependency
order. The old `RT-*` tickets were removed with the spike they tracked
(UC-000). Findings no ticket captures live in [`findings.md`](findings.md).

## Done

UC-000 through UC-010: spike retired; thread-local image, blocks, install,
and fiber binding; suspension; the task's ring; scheduler zero; main
dispatched through it.

## Open

| id | waits on |
|---|---|
| UC-013 | nothing — park fibers on the ring, the reactor iteration |
| UC-011 | UC-009 landed; wants spawn/recycle measurements |
| UC-012 | FSGSBASE-capable x86-64 hardware |
