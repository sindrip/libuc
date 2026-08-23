# Measurements

Status: measured 2026-08-23 on the development machine — Apple Silicon,
QEMU 11.1.0 + hvf, 1 vCPU, guest Linux 7.2. Numbers are for this substrate.
They are reproducible here, which is what makes them useful for comparing
changes against each other.

## Method

`rt_ticks()` reads `CNTVCT_EL0`, preceded by `isb` so the read is not
speculated past. `CNTFRQ_EL0` reports **24 MHz**, so one tick is 41.67 ns —
far too coarse for a single operation, which is why every figure below is a
loop of 20k–1M iterations divided by the count. `rt_ticks()` itself costs
~10.6 ns, dominated by the `isb`; bracketing a 200k-iteration loop with two of
them is a rounding error.

The counter read is an instruction, not a syscall, so it raises no question
under invariant 1.

**Every figure is min/median/max over five cold boots.** Single runs are
misleading here: the ring figure alone swings 38% between boots, which is
wider than several of the effects being measured. `libuc.md` records a
performance claim that was made on one run of each and did not survive.

**The harness is not in the tree.** It was ~220 lines of never-executed code in
`main.c` plus a `rt_ticks`/`rt_tick_hz` pair with no other caller, which is the
kind of thing this codebase deletes rather than carries. The findings below are
the deliverable; the method above is enough to rebuild it when a number needs
rechecking. It was last run at `36ef8cb`, recoverable from there if wanted.

## Runtime primitives

| | min | median | max |
|---|---|---|---|
| `rt_fiber_current()` | 1.02 | **1.16** | 1.16 |
| wake — bare switch pair | 13.81 | **14.46** | 14.97 |
| yield round trip — switch, dispatch, queue, switch | 19.45 | **23.27** | 23.82 |
| spawn + exit + yield | 58.49 | **61.19** | 62.93 |
| bare syscall (`sigaltstack(NULL, &old)`) | 71.58 | **77.29** | 77.97 |
| empty `io_uring_enter` (0 submit, 0 complete) | — | **89.17** | — |
| ring NOP, one op in flight | 238.76 | **256.65** | 260.72 |

ns per operation.

`rt_fiber_current()` at ~1.2 ns is the masking implementation: frame address,
mask, load, magic compare, load. It is a throughput figure over a million
independent calls, not a latency one.

`scheduler.md` claimed a WAKE round trip costs "roughly 12 ns". Measured at
14.5, so the claim was approximately right and is now grounded.

## Is blocking-as-suspension cheap? Not below ~30 concurrent operations

`libuc.md` asks this as an open question: switch plus ring round trip against
a plain syscall. The comparison depends entirely on how many operations are in
flight, because one `io_uring_enter` submits all of them.

| fibers with an op in flight | ns/op (median) |
|---|---|
| 1 | 218.57 |
| 4 | 106.28 |
| 16 | 88.29 |
| 30 | 76.11 |
| — bare syscall, for comparison | **77.29** |

**The ring breaks even against a plain syscall at roughly 30 concurrent
operations, and loses badly below about 8.** At one operation in flight it
costs 2.8x the syscall it replaces.

The curve is still descending at 30, so the crossover is a ceiling rather than
an asymptote — 30 is simply the largest that fits, since `RT_FIBER_MAX` is 32.

### Where the 220 ns actually goes

Measured, not apportioned by arithmetic:

| part | ns | share |
|---|---|---|
| our switch pair, queues, staging | ~14 | 7% |
| the syscall itself — empty `io_uring_enter` | 89 | 40% |
| io_uring's submit, execute, complete | ~117 | 53% |

**The runtime's own machinery is about 7% of a ring operation.** Making our
code infinitely fast would take 220 ns to 205. Anyone optimising the ring path
should start with the number of enters, not with `scheduler.c`.

`io_uring_enter` is also not an expensive syscall — 89 ns against a 74 ns
floor, 20% over the cheapest syscall that exists. So the per-op cost at low
concurrency is not io_uring being heavy. It is that **one op in flight means
one syscall per op**, which is exactly what a syscall-based design does. The
ring cannot win on that count, and loses by the SQ/CQ bookkeeping each op
carries on top. The whole advantage is amortising one enter across many ops,
which the curve above shows working.

The lever this points at is fewer enters. `io_uring/loop.c` and `bpf-ops.c`
run the event loop in-kernel as a BPF struct_ops callback, removing the enter
from the per-op path entirely; plan.md keeps that reachable and this is the
first measurement arguing for it.

Two things this does not say. It uses `IORING_OP_NOP`, which isolates the cost
of the mechanism by doing no work; a real `recv` has kernel work that dominates
the difference. And it is one thread — the design's actual argument for the
ring is that a suspension does not block the thread, which a syscall does, and
that difference does not appear in a single-threaded microbenchmark at all.

So the honest reading is narrower than the question: the ring's *mechanism* is
not cheaper than a syscall until concurrency pays for the enter. Whether
blocking-as-suspension is cheap in the sense libuc means — that a blocked
fiber costs nothing while others run — is a different measurement that needs
a workload where threads would actually block.

## Same operation, syscall against ring: the syscall wins

The question the NOP figures do not answer: for identical work on one thread,
is the ring faster in wall clock? Measured with `write(1, buf, 0)` — the same
call, in the same binary, once through `raw_write` and once through
`rt_write`, so both take the same VFS path and neither touches the device.

| | median ns/op |
|---|---|
| via raw syscall | **160.13** |
| via ring, 1 fiber | 362.29 |
| via ring, 4 fibers | 235.23 |
| via ring, 30 fibers | 201.97 |

**The ring loses at every concurrency this machine can reach** — 2.3x at one
op in flight, still 1.26x at thirty. Batching amortises `io_uring_enter` down
to about 3 ns/op and no further, because the rest is per-operation: writing a
64-byte SQE, reading the CQE, two queue operations, `owner` and counter
bookkeeping, and the 14 ns switch pair. Call it ~40 ns per op that a syscall
does not pay.

Being fixed, that overhead matters in proportion to how cheap the operation
is. A zero-length write is near the syscall floor at 160 ns, so 40 ns is 26%.
Against a socket `recv` costing microseconds of kernel work it is noise.

**And the syscall loop cannot do the job.** It is a straight line of writes in
one fiber: no multiplexing, nothing else can run. The ring version is
interleaving thirty independent fibers and paying for that. The comparison
that would actually decide a server design is epoll plus non-blocking syscalls
against fibers plus ring — where epoll pays `epoll_wait` and per-fd
bookkeeping, and cannot use blocking calls at all. **That has not been
measured, and it is the one that matters.**

So the defensible claim is narrow: for a fixed sequence of operations on one
thread, raw syscalls are faster, always. The ring buys operations outstanding
while the thread does other work, and a benchmark with nothing else to do
gives it no way to show that.

## Echo server, end to end

Measured from the host through QEMU's user-mode networking with the port
forward, 64-byte payloads, median of three runs:

| | |
|---|---|
| round trips, one connection | 18,070 /s — 55.3 us each |
| connect + echo + close | 3,284 /s |
| round trips, 16 concurrent connections | 29,062 /s |

**These measure the harness, not the runtime.** A 55 us round trip against a
250 ns ring round trip means the runtime is well under 1% of it; the rest is
virtio-net, slirp in QEMU's main loop, and a Python client. 16 concurrent
connections reach only 1.6x the single-connection rate, which is the shape of
a single-threaded bottleneck in QEMU rather than anything in the scheduler.

Useful as a regression check — a change that halves this broke something — and
useless as a performance figure. A real network number needs vhost/tap or bare
metal.
