# io_uring-native runtime — foundation

## Context

Greenfield project (no commits yet). The goal is to learn C by building a
runtime whose defining constraint is the target rather than the language:
**Linux first, io_uring as the syscall ABI**. The language the runtime will
eventually serve is deliberately deferred — the runtime is a C library whose
"programs" are hand-written C functions, and the language question gets
answered later by what the runtime actually needs.

The repo already contains a working kernel/VM pipeline (`build/kernel.Dockerfile`,
`docker-bake.hcl`, `run.sh`). This plan adds the runtime itself, plus two small
fixes to the existing pipeline. Performance is explicitly out of scope for now —
bare metal comes later; today's target is an Apple Silicon VM where core pinning
is architecturally meaningful but not measurable.

## Decisions already settled

| Decision | Choice |
|---|---|
| Purity | Every op with an opcode goes through the ring. Direct syscalls only where no opcode exists |
| Ring flags | `SINGLE_ISSUER \| DEFER_TASKRUN`, **never** `SQPOLL` |
| Task | Stackful coroutine, hand-rolled aarch64 asm switch |
| Cores | Strict shared-nothing, no migration, no work stealing |
| Threads | `clone()`, one address space (isolation by discipline, not MMU) |
| libc | None — true freestanding |
| Language | C23 (`-std=c23`) |
| Toolchain | Clang for the runtime, GCC for the kernel |
| Userspace | Runtime is PID 1 |
| io-wq | Designed never to trigger; low `IOWQ_MAX_WORKERS` as a tripwire |
| Preemption | None. Cooperative + `rt_yield()` + ring-native watchdog |
| BPF loop | Not now, but stay reachable |

Verified against the pinned 7.2 tree in `out/src/`:

- `io_uring/io_uring.c:2815-2821` — `SQPOLL` rejects `COOP_TASKRUN`,
  `TASKRUN_FLAG`, and `DEFER_TASKRUN` with `-EINVAL`. Zero-syscall steady state
  is off the table by construction.
- `include/uapi/linux/io_uring.h:312-313` — `IORING_OP_BIND` / `IORING_OP_LISTEN`
  exist, so socket→bind→listen→accept→recv→send→close is entirely ring-native on
  7.2.
- `io_uring/register.c:860`, `include/uapi/linux/io_uring.h:674` —
  `IORING_REGISTER_IOWQ_AFF` survives as the io-wq escape hatch.
- `include/uapi/linux/io_uring.h:253` — `IORING_SETUP_SQ_REWIND` (needs
  `NO_SQARRAY`, excludes `SQPOLL`) suits a batch-submit-once-per-turn scheduler.
- `io_uring/loop.c`, `io_uring/bpf-ops.c` — the event loop can run in-kernel as
  a BPF struct_ops callback (`bpf_io_uring_submit_sqes`,
  `bpf_io_uring_get_region`). The existing `kernel.config` already enables it.

## Milestone 1 — one ring, one coroutine, one NOP

Boot as PID 1, submit `IORING_OP_NOP`, reap the CQE, resume a stackful
coroutine through the asm switch, write `hello` to the console via
`IORING_OP_WRITE`, exit. `NOP` is deliberate: no fd, no I/O, so a failure is
unambiguously the ring code.

This exercises every novel subsystem exactly once. Everything afterwards is
addition rather than discovery.

### Files to create

```
src/start.S      _start: sp points at argc; align, call main, exit_group
src/switch.S     ctx switch: x19-x28, x29, x30, sp, d8-d15
src/syscall.h    svc #0 wrappers; x8=nr, x0-x5=args; ret is -errno in -1..-4095
src/string.c     memcpy, memset, memmove, memcmp  <- when a link first needs them
src/ring.c/.h    io_uring_setup + mmap; submit/reap; no liburing
src/task.c/.h    task struct, stack alloc, spawn, yield, resume
src/main.c       milestone 1 driver
```

### Freestanding pitfalls, in the order they will bite

1. **The compiler emits calls to `memcpy`/`memset`/`memmove`/`memcmp`** even
   under `-ffreestanding` — struct assignment and array init generate them. All
   four must exist or you get link errors that look like build misconfiguration.
2. `-fno-stack-protector`, or `__stack_chk_fail` is undefined.
3. `_start` is yours: `-nostdlib -nostartfiles`.
4. No `errno`. Raw returns are `-errno` in `-1..-4095`.
5. **PID 1 must never return** — the kernel panics with `Attempted to kill init`.

### C23 — why it matters more than usual here

C23 is a real win for a *freestanding* project, because it enlarged what
freestanding is required to provide:

- **`static_assert` is a keyword** (no `<assert.h>`). Use it for assumptions the
  type system cannot express — not for struct layouts, which come from the
  kernel's own headers via `make ARCH=arm64 headers` and never retyped by hand. The
  motivating case: `sizeof(struct io_uring_cqe)` is 16 even under
  `IORING_SETUP_CQE32`, because `big_cqe[]` is a flexible array member, so ring
  *stride* comes from the setup flags rather than the type. Assert the
  flags-versus-arithmetic relationship, not the size. See RT-005.
- **`<stdckdint.h>`** — `ckd_add`/`ckd_sub`/`ckd_mul`. Buffer and ring-index
  arithmetic without hand-rolled overflow checks. Shipped by both GCC and Clang
  as a *compiler* header, so it survives `-nostdlib`.
- **`<stdbit.h>` is not usable — do not plan around it.** The two headers are
  inverted from what you would expect: C23 lists `<stdbit.h>` as
  freestanding-*required* and `<stdckdint.h>` as not, yet every toolchain
  implements `<stdbit.h>` as a *libc* header (glibc 2.39+) and `<stdckdint.h>`
  as a compiler header. Verified absent from GCC 15.2, Clang 22.1.3, musl, and
  Apple clang 21. Since this project links no libc, it is unreachable regardless
  of base image or compiler version — this is a toolchain conformance gap, not a
  configuration mistake ([LLVM #62248](https://github.com/llvm/llvm-project/issues/62248)
  asks exactly who owns these). Ring sizing uses `__builtin_clzll` /
  `__builtin_ctzll` instead, which are intrinsics needing no header.
- `bool`/`true`/`false` are keywords — `<stdbool.h>` no longer needed.
- `nullptr`, `constexpr`, `auto`, standardized `typeof`, `[[noreturn]]`,
  `unreachable()`, binary literals.
- Empty `()` now means `(void)` — removes a whole class of C footguns.
- `#embed` — a clean way to embed a BPF object later, if milestone 4 happens.

`<stdatomic.h>` remains available, so the ring's acquire/release barriers work
on day one. (Atomics are technically optional — check `__STDC_NO_ATOMICS__` —
but GCC and clang both provide them.)

### Toolchain: Clang for the runtime, GCC for the kernel

Verified on the pinned Alpine: **Clang 22.1.3** and **GCC 15.2.0**, both fully
adequate for the C23 this project uses (`constexpr`, `auto`, `nullptr`, `typeof`,
`[[nodiscard]]`, binary literals, keyword `static_assert`, `<stdatomic.h>`,
`<stdckdint.h>` — all confirmed present on both). C23 support did **not** decide
this; neither compiler fully conforms and both miss `<stdbit.h>` identically.

Three things decided it, none about C23:

1. **BPF is Clang-only in practice.** The in-kernel loop is a retained direction
   and the kernel config already enables it, so Clang enters the project anyway.
2. **`lldb` is the only debugger on this machine** and pairs with Clang's debug
   info. Debugging is load-bearing here — see RT-002 and RT-007.
3. **Freestanding UBSan**, below. GCC has no minimal-runtime mode.

The kernel stays on GCC: it builds today, switching is orthogonal risk, and the
runtime shares no code with it. Both are native aarch64 under OrbStack, so no
cross-compiler is needed either way.

### Build flags

```
-std=c23 -ffreestanding -nostdlib -nostartfiles -static
-fno-stack-protector -fno-omit-frame-pointer -g -O1
-fsanitize=undefined -fsanitize-minimal-runtime -fno-sanitize-recover=all
```

`-static` is load-bearing, not stylistic: fixed load addresses are what make
the QEMU gdbstub usable without guessing ASLR offsets. It also implies non-PIE,
so `-no-pie` is redundant and clang errors under `-Werror` that it went unused.

The UBSan flags are the closest thing this project has to a test suite, given
testing is manual console inspection with no regression net. Verified working
freestanding: the build emits undefined `__ubsan_handle_*_minimal_abort` symbols
— one per check kind actually triggered — which you implement yourself and route
into the RT-007 crash handler. That buys runtime detection of shifts past width,
misaligned loads, and signed overflow, in precisely the hand-rolled pointer and
ring-index arithmetic where nothing else is watching.

### Ring setup for milestone 1

`SINGLE_ISSUER | DEFER_TASKRUN | NO_SQARRAY`. `NO_SQARRAY` is *simpler*, not
just faster — there is no indirection array to maintain. Add `SQ_REWIND` later
as a further simplification once submission batching exists.

Probe with `query.c`'s capability interface (`nr_request_opcodes`,
`feature_flags`) rather than hardcoding kernel assumptions.

## Fixes to the existing pipeline

**`debug.sh` — landed, and vfkit is gone.** `vfkit` has no gdbstub;
Virtualization.framework exposes no debug interface at all. Rather than keep two
launchers, `run.sh` moved to QEMU and `debug.sh` delegates to it with `-s -S`,
so machine, cpu, memory and console cannot drift between them. Verified: lldb
attaches, halts at the `Image` entry `0x40000000`, reads registers, and steps.

Dropping vfkit cost one thing worth naming: **hvf cannot do SMP** (QEMU 11.1.0,
`-smp >1` hangs silently at every gic-version), so QEMU's multi-core path is
TCG-only and slow. Milestone 3 must pick TCG, bring vfkit back alongside, or be
on bare metal by then. For milestones 1–2 QEMU+hvf is strictly better than
vfkit — same speed class, plus a debugger. Full findings in RT-002.

**The initramfs returns in RT-003.** The pipeline has since been stripped to
kernel-only: `run.sh` boots to a deliberate no-init panic, and `build/init` and
`busybox-static` are gone. RT-003 reintroduces an initramfs holding exactly one
file — the runtime, as `/init`. No rescue shell: the PID-1 decision stands, and
debugging is `./debug.sh` + `lldb` plus the RT-007 crash handler, which is why
both are sequenced ahead of the hard parts.

**`run.sh` has no network device** — `--device virtio-serial,stdio` only. Add a
`virtio-net` device before the echo server milestone; also confirm
`CONFIG_VIRTIO_NET=y` survives in `out/kernel.config`, since the base is now
`tinyconfig` rather than `virtconfig`. Not needed for milestone 1.

**Preemption: deliberately left unset.** `out/kernel.config` shows `tinyconfig`
on 7.2/arm64 resolving to `CONFIG_PREEMPT_LAZY=y` — not `PREEMPT_NONE`, which is
worth knowing since it contradicts the obvious assumption. The fragment does not
state it, because the fragment's rule is that it carries only what we ask for,
and nothing currently depends on the preemption model: one pinned runnable
thread per core with nothing competing means there is almost nothing to preempt.

Two things were tried and rejected:

- `CONFIG_PREEMPT_DYNAMIC=y`, which would allow `preempt=none|voluntary|full|lazy`
  at boot. It resolves fine alongside `PREEMPT_LAZY`, but pulls in
  `CONFIG_PREEMPT_RCU=y` — preemptible RCU read-side sections plus an
  `rcu_preempt` kthread — which is unrequested behaviour today in exchange for a
  measurement capability that is out of scope until bare metal.
- `PREEMPT_RT`, on architectural grounds: it threads IRQ handlers and softirqs
  onto our cores, the same interference class RT-008 exists to detect. Also note
  `rt` here means *runtime*, not realtime; nothing in this design is about hard
  latency bounds.

The accepted cost is drift: a kernel bump could move the default silently. The
mitigation is that `docker buildx bake config` resolves Kconfig without
compiling, so `grep PREEMPT out/kernel.config` answers the question in seconds.
Revisit on bare metal, with a benchmark, where the answer can actually be
determined.

**Pin the Alpine tag — landed.** The toolchain decides which C dialect the
runtime compiles as, so an upstream Alpine retag could change the language
silently. The digest is pinned directly on the `FROM` line in
`build/kernel.Dockerfile`. Routing it in from `docker-bake.hcl` was tried both
ways and rejected: neither a named build context nor a global `ARG` can be
declared once, so a pin belonging to a single stage ends up restated on every
target that reaches it — and the context form additionally leaves a bare
`docker build` silently unpinned.

The digest provides alpine 3.24.1 with **Clang 22.1.3** and **GCC 15.2.0** —
both clear the C23 bar comfortably, so the dialect was never actually at risk;
the reproducibility was. Full measurements in RT-001.

**Crash handler.** No core dumps, no `dmesg` afterwards. A `SIGSEGV`/`SIGBUS`/
`SIGILL` handler that dumps registers and walks the frame-pointer chain to
`hvc0` is worth real time; `-fno-omit-frame-pointer` from day one makes it
possible.

## Later milestones

2. **Single-core echo server** — `SOCKET`/`BIND`/`LISTEN`/`ACCEPT`/`RECV`/`SEND`
   as opcodes. Needs virtio-net. Decide provided buffer rings
   (`IORING_REGISTER_PBUF_RING`) + multishot recv here.
3. **Multi-core** — `clone()`, `sched_setaffinity`, N rings, one per pinned
   thread. Watchdog: each scheduler arms an `IORING_OP_TIMEOUT`, bumps its own
   relaxed atomic tick, and reads its neighbour's in a ring — core *i* watches
   core *i+1*. Fully ring-native, no watchdog thread, no extra core.
4. **Cross-core transport** — still open. `MSG_RING` gives a ring-native
   doorbell but only 96 bits, so the payload/ownership question is unresolved.
   Not needed before milestone 3.

## Deliverables — `.scratch/`

```
.scratch/plan.md               this document
.scratch/tickets/RT-00N-*.md   one file per ticket
```

Add `.scratch/` to `.gitignore` (currently only `out/`) unless you want the
tickets committed — repo has no commits yet, so it's a free choice.

Each ticket file carries: `id`, `status`, `depends`, then **Goal**, **Spec**,
**Files**, **Acceptance**, **Notes**.

### The one sanctioned purity exception

`raw_write()` — a direct `write` syscall to the console. Required because every
ticket below RT-005 has no ring, and because ring *setup failure* and the crash
handler must be able to report when the ring is unusable or in unknown state.
Used nowhere else. Documented as a carve-out, not discovered as a violation.

### Tickets

| id | title | depends | acceptance |
|---|---|---|---|
| RT-001 | Pin Alpine by digest, verify C23 | — | **done** — Clang 22.1.3 / GCC 15.2.0, C23 OK, `<stdbit.h>` absent on both |
| RT-002 | `debug.sh` — QEMU + gdbstub | — | **done** — `lldb` → `gdb-remote localhost:1234` halts at kernel entry |
| RT-003 | Freestanding skeleton | 001 | **done** — Boots as PID 1, `raw_write`s a byte to `hvc0`, spins; no panic |
| RT-004 | Context switch + task struct | 002, 003 | Spawn → switch in → switch out → switch in again; verified under gdbstub |
| RT-005 | Raw ring setup + NOP | 003 | `io_uring_setup` succeeds; NOP SQE submitted, CQE reaped, `res == 0` |
| RT-006 | **Milestone 1** — join them | 004, 005 | Task suspends on NOP, scheduler resumes it, `hello` via `IORING_OP_WRITE`, no panic |
| RT-007 | Crash handler — **do before RT-004** | 003 | Deliberate null deref dumps registers + FP chain to console |
| RT-008 | io-wq tripwire | 005 | `IOWQ_MAX_WORKERS` registered low; no `iou-wrk-*` in `/proc/<pid>/task` |

**Execution order is 001, 002, 003, 007, 004, 005, 006, 008** — not the
numbering. RT-001 and RT-002 are independent of the runtime: RT-001 decides
which language dialect actually compiles, and RT-002 must exist before RT-004
because debugging hand-rolled aarch64 context-switch asm without a debugger is
the failure mode this whole ordering exists to avoid. RT-007 moves ahead of
RT-004 for the same reason: testing is manual console inspection with no
regression net, so the crash handler is the only diagnostic the project has when
`switch.S` corrupts something.

RT-005 takes its structs from `<linux/io_uring.h>` via `make ARCH=arm64 headers`; kernel
types are never retyped by hand. Its `static_assert`s guard the one thing the
types cannot say — that the setup flags exclude `SQE128`/`CQE32`, so the ring
stride the arithmetic assumes matches the ring that was actually created.

## Open questions

- Cross-core message transport and buffer ownership (deferred, needed at #3).
- Offload path for genuinely CPU-bound work that steps outside the model.
- Per-core arena allocator design and task stack sizing/guard pages.

## Verification

First, confirm the toolchain supports the language we just chose:

```sh
docker run --rm alpine sh -c 'apk add -q gcc && gcc --version | head -1 &&
  echo "int main(){constexpr int x=0b1010; static_assert(sizeof(int)==4); return x;}" \
  > /t.c && gcc -std=c23 -ffreestanding -fsyntax-only /t.c && echo C23_OK'
```

And that Kconfig resolved what we think it did — this costs no compile:

```sh
docker buildx bake config
grep -E 'PREEMPT|IO_URING|NET_RX_BUSY_POLL' out/kernel.config
```

Then milestone 1 is done when:

```sh
docker buildx bake kernel     # vmlinuz + initramfs with the runtime as /init
./run.sh                      # QEMU + hvf, 1 vCPU
```

prints `hello` on `hvc0` and stays alive without a kernel panic — a panic being
the signal that PID 1 returned. Note the current baseline is a *deliberate*
`VFS: Unable to mount root fs` panic, so "no panic" only becomes meaningful once
RT-003 lands.

Then confirm the architecture's central claim holds. There is no shell, so the
runtime reports on itself: read `/proc/self/task` **through ring opcodes** and
`raw_write` the count — 1 at milestone 1, N at milestone 3. The absence of
`iou-wrk-*` entries is the check that the io-wq tripwire is holding and that no
accidental punt path has crept in. `./debug.sh` is the independent second
opinion.

Debug with `./debug.sh` + `lldb` → `gdb-remote localhost:1234`.
