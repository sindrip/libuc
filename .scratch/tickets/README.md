# Tickets

The active queue is [`UC-*`](uc/): small libuc changes that first establish the
fiber and its C thread-local identity, then make the initial Linux task a
single scheduler and run the root fiber through it. Numbers follow dependency
order. The old `RT-*` tickets have been removed, and their runtime — the spike
— was retired by UC-000. Findings no ticket captures live in
[`findings.md`](findings.md).

## Active order

| id | result |
|---|---|
| UC-000 | Retire the spike |
| UC-001 | Record the executable's static thread-local image |
| UC-002 | Create and switch bare fibers, with no scheduler |
| UC-003 | Enter `main` on the root fiber |
| UC-004 | Create and destroy independent thread-local blocks |
| UC-005 | Install any thread-local block as the current one |
| UC-006 | Make the thread-local block part of fiber context |
| UC-007 | Suspend and resume a fiber, with no scheduler |
| UC-008 | Create and operate the current task's io_uring |
| UC-009 | Make the current Linux task a scheduler |
| UC-010 | Run the root fiber through scheduler zero at startup |
