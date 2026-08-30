# Tickets

The `UC-*` queue under [`uc/`](uc/) established the fiber and its C
thread-local identity, then made the initial Linux task a single scheduler
running the root fiber through its own ring. Numbers follow dependency
order. The old `RT-*` tickets were removed with the spike they tracked
(UC-000). Findings no ticket captures live in [`findings.md`](findings.md).

## Done

UC-000 through UC-010, UC-013, UC-014, and UC-015: spike retired;
thread-local image, blocks, install, and fiber binding; suspension; the
task's ring; scheduler zero; main dispatched through it; fibers parked and
woken through the ring; ready sweeps bounded to one generation;
pipe/read/write/close and per-fiber errno as the first public libc surface.

## Open

| id | waits on |
|---|---|
| UC-011 | nothing — make scheduler ownership explicit, then pool stack/block storage |
| UC-016 | nothing — multi-CQE operations: identity and stream delivery |
| UC-017 | UC-016 — cancellation, zombie lifetime, slot recycling |
| UC-018 | nothing — single-shot sockets: the connection calls over the ring |
| UC-012 | FSGSBASE-capable x86-64 hardware |
