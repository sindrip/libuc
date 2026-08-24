# Tickets

The active queue is [`UC-*`](uc/): small libuc changes that compose into one
goal — enter `main` on a fiber without making a scheduler part of process
startup. The old `RT-*` tickets have been removed; `src/` is frozen and is not
an active roadmap for libuc.

## Active order

| id | result |
|---|---|
| UC-001 | Record the executable's static thread-local image |
| UC-002 | Create and destroy independent thread-local blocks |
| UC-003 | Install any thread-local block as the current one |
| UC-004 | Create and switch bare fibers, with no scheduler |
| UC-005 | Make the thread-local block part of fiber context |
| UC-006 | Enter `main` on the root fiber |
