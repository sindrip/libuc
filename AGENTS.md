# AGENTS.md

## What this is

A runtime built on io_uring, in freestanding C23, running as PID 1 on a pinned
Linux kernel inside a minimal VM image. No libc. No liburing. No language on top
of it — yet.

The eventual goal is a runtime for a programming language. The language is
deliberately deferred: right now the runtime is a C library whose "programs" are
hand-written C functions, and the language question gets answered later by what
the runtime actually turns out to need.

## Motivation

Most runtimes start from a language and adapt to whatever the OS offers. This one
inverts that: **start from the target.** Linux 7.2 and io_uring are the fixed
point, and the runtime's shape is derived from them rather than negotiated with
them.

The secondary goal is learning C properly — which is why the hard paths are taken
deliberately (hand-rolled context switching, raw ring mechanics, no libc) rather
than delegated to a library.

## North star

The destination is **libuc**: this runtime packaged as the C library — a libc
whose blocking calls are fiber suspensions over the ring, with a C ABI surface
good enough to host C code we choose to vendor. "No libc" has always meant
never *linking* one; becoming one is the goal.

PID 1, the QEMU harness, and the single pinned kernel are scaffolding, not
identity: they buy a noise-free machine for developing the scheduler and ring
mechanics. When the runtime later runs hosted, the kernel pin becomes a version
*floor* plus capability probing at init — never a compatibility matrix.

Read the invariants in that light. The ring as syscall ABI, shared-nothing
schedulers, and cooperative fibers are the product and survive into libuc
unchanged. The absences — no `errno`, no allocator, no TLS — are phase
discipline: each returns as a libuc deliverable when vendored C code first
needs it, per-fiber where the C world assumed per-thread. Vendored static
libraries come first; being the platform libc for whole foreign programs is a
separate, later question the north star does not require answered.

## Invariants — do not violate these without discussion

These are the design. Code that breaks one is wrong even if it works.

1. **io_uring is the syscall ABI.** If an operation has an opcode, it goes
   through the ring. `openat`, `close`, `socket`, `bind`, `listen`, `accept`,
   `read`, `write`, `timeout`, `futex` — all opcodes on 7.2. Direct syscalls are
   permitted **only** where no opcode exists: `mmap`, `mprotect`, `munmap`,
   `clone`, `sched_setaffinity`, `rt_sigaction`, `sigaltstack`, `io_uring_*`,
   `exit_group`.
2. **Never `IORING_SETUP_SQPOLL`.** It is mutually exclusive with
   `DEFER_TASKRUN`, `COOP_TASKRUN`, and `TASKRUN_FLAG`
   (`out/src/io_uring/io_uring.c:2815-2821`). Rings are
   `SINGLE_ISSUER | DEFER_TASKRUN | NO_SQARRAY`.
3. **Shared-nothing, no migration.** A fiber is born on a scheduler and dies
   there. No work stealing. No allocation on one scheduler and free on another.
   Per-scheduler state is reached without atomics; the only shared state is
   explicitly designated as such.

   **The unit is the scheduler, not the core.** The kernel binds a ring to a
   *task*: `ctx->submitter_task = get_task_struct(current)`
   (`out/src/io_uring/io_uring.c:3067`), enforced against `current` at
   `tctx.c:202-204`, `register.c:764`, `rsrc.c:1433-1434`, `tw.c:313` and
   `tw.h:108`. "One scheduler, one thread" is therefore what is actually
   enforced, and everything a scheduler owns — ring, arena, stack pool, buffer
   pool — is owned per scheduler. How many schedulers share a core is a
   program's placement decision, not a rule of this design.

   This is the invariant, and it holds. `.scratch/scheduler.md` records what
   migration *would* cost rather than permitting it — the design deliberately
   avoids foreclosing it, which is not the same as allowing it. Anything that
   actually moves a fiber between schedulers violates this line and needs the
   discussion the heading asks for.
4. **No libc, ever.** `-ffreestanding -nostdlib`. Two header sources are
   permitted, and nothing else:
   - What the *compiler* ships: `<stdint.h>`, `<stddef.h>`, `<stdarg.h>`,
     `<stdatomic.h>`, `<stdckdint.h>`. **Not `<stdbit.h>`** — C23 lists it as
     freestanding-required, but every toolchain implements it as a *libc*
     header (glibc 2.39+), so it is absent from GCC 15.2, Clang 22, and musl
     alike. Use `__builtin_clzll` / `__builtin_ctzll` / `__builtin_popcountll`;
     they are intrinsics and need no header.
   - The **pinned kernel's own uapi headers**, via `make headers_install` from
     the tree already in the build. `<linux/io_uring.h>`, `<asm/unistd.h>`, and
     friends are declarations only — nothing is emitted, nothing is linked, and
     they are the same SHA256-pinned bytes `out/src/` is the authority for.

   "No libc" is about not *linking* a C library. Consuming the kernel's ABI
   definitions is the opposite of a dependency, and **retyping them by hand is
   forbidden**: it duplicates a definition that is already in the repo and
   manufactures a class of silent layout and semantic bugs for no benefit. Read
   the headers; do not copy them.
5. **Do not add liburing.** Ring mechanics are hand-written on purpose.
6. **PID 1 must never return.** The kernel panics with `Attempted to kill init`.
   `rt_main` ends in a loop or a deliberate reboot.
7. **Cooperative scheduling only.** No preemption. Fibers yield at I/O points
   or via `rt_fiber_yield()`.
8. **One purity exception exists.** See below. Do not add a second without
   recording it there.

## The purity exception registry

| symbol | what | why it is allowed |
|---|---|---|
| `raw_write()` | direct `write(2)` to console | Needed before the ring exists, to report `io_uring_setup` failure, and from the crash handler when the ring may be corrupt. |

That is the complete list. If you believe you need another, say so explicitly
rather than adding it quietly.

## Ground truth: read the kernel, do not recall it

`out/src/` holds the **whole pinned 7.2 tree** — `io_uring/`, the uapi headers,
and everything else. It is the authority for every question about opcodes,
flags, struct layouts, and behaviour.

**Do not answer io_uring questions from memory or from man pages found online.**
7.2 postdates most training data and contains things that do not exist in older
kernels — `IORING_OP_BIND`/`LISTEN`, `IORING_SETUP_SQ_REWIND`, `query.c`
capability probing, and `loop.c`/`bpf-ops.c` (an event loop that runs in-kernel
as a BPF struct_ops callback). Man pages describing 6.x are actively misleading
here.

Read `out/src/`. Cite file and line when making a claim about kernel behaviour.

If `out/src/` is missing, regenerate it with `./src.sh` — opt-in, ~1.7 GB,
~95k files, about 40s. Use the script rather than `bake src` directly: no
exporter prunes, so it clears `out/src` first, which is what stops a
`KERNEL_VERSION` bump leaving files deleted upstream behind in the directory
whose whole job is being accurate.

## Layout

```
meson.build               libuc — the deliverable; Clang + LLD, one static libc.a
meson.options             tests / ubsan toggles
include/                  the headers libuc installs; the install manifest
src/                      the implementation
  start.c                 _start's C half
  syscall.h               raw syscall wrappers; -errno in -1..-4095
  ubsan.c                 the minimal-runtime UBSan handlers
  thread_local/           the PT_TLS image and the per-block lifetime
  errno/                  <errno.h>: the per-fiber errno datum
  string/                 <string.h>: memcpy, memset, memmove, memcmp
  sys/auxv/               <sys/auxv.h>: the parsed auxiliary vector, getauxval
  arch/<arch>/            everything that knows the instruction set
    start.S               _start
    syscall_arch.h        the trap sequence and register assignment
    thread_local_arch.h   the TLS variant the ABI fixes for the arch
    string/               the vectorised memcmp block
test/                     startup probes and their program-header checks
tools/mkinitramfs.sh      wrap a probe as an initramfs image
cross/                    meson cross files; runnable.ini layers exe-wrapper off
libuc.ld                  linker script for the static probes
build/kernel.Dockerfile   SHA256-pinned fetch, toolchain, Kconfig, kernel build
build/kernel.config       fragment merged over tinyconfig + kvm_guest.config
docker-bake.hcl           targets: `kernel` (default), `config`, `src`, `uapi`, `uapi-x86_64`
run.sh                    boot under QEMU (hvf; ACCEL/SMP/INITRD overridable)
debug.sh                  same boot, halted, gdbstub on :1234
src.sh                    export the pinned kernel tree -> out/src/
```

**A `src/` directory mirrors the installed header it implements** — the
header's path minus `.h`, so `<string.h>` is `src/string/` and
`<sys/auxv.h>` is `src/sys/auxv/`; every other directory is runtime
machinery with no installed surface. The mapping is mechanical on purpose:
no topical grouping, no `misc/`, and basename twins like a future
`<time.h>` / `<sys/time.h>` get distinct homes. A header that installs
only types or constants has no TU and therefore no directory.

**The architecture is chosen by the include path, never by generic code.**
Each consuming meson target adds `src/arch/` + the host `cpu_family` to its
include directories, so `syscall.h` says `#include "syscall_arch.h"` and the
build decides which one that is. Generic headers naming `arch/aarch64/...`
directly is the failure this prevents: it puts one architecture inside the
code that is supposed to be portable, and the second architecture then arrives
as an `#ifdef` ladder. Adding one should be a directory plus a cross file,
touching no generic file.

The paired header keeps the `_arch` suffix on purpose. Naming it `syscall.h`
inside `arch/<arch>/` would make `src/syscall.h`'s own include resolve to
itself, since a quoted include searches the including file's directory first.

```
out/                      GENERATED — gitignored, never edit
out/vmlinuz               the kernel
out/kernel.config         what Kconfig actually resolved — read this, not the fragment
out/System.map            kernel symbols
out/uapi*/include/        the pinned kernel's uapi headers, per architecture
out/src/                  whole pinned kernel tree, read-only ground truth
.cache/meson-<arch>/      meson build directories; compile_commands.json symlinks
                          into the aarch64 one for clangd
.scratch/plan.md          design decisions and rationale
.scratch/tickets/         RT-00N (closed) and uc/UC-00N (active)
```

Dockerfile stages: `linux-tarball` → `toolchain` → `kernel-tree` → `kconfig` →
`kernel-build`, plus the uapi header installs, with `scratch` export stages
hanging off them — `linux-src`, `config`, `kernel`, `uapi`, `uapi-x86_64`.

`linux-tarball` is the only stage that knows `KERNEL_VERSION`: it `ADD --unpack`s
the tarball to `/`, leaving the archive's own `linux-$V/` at the stage root.
`ADD` has no `--strip-components`, so the prefix is dropped one stage later by
`COPY --from=linux-tarball /linux-*/ /linux/` — a `COPY` of a directory takes its
contents, not the directory, so the glob both matches the versioned name and
discards it. Everything downstream of `kernel-tree` sees a version-free `/linux`,
and a version bump does not invalidate the toolchain's apk layer.

The kernel tarball is pinned in `docker-bake.hcl` as `KERNEL_VERSION` and
`KERNEL_SHA256`, reaching `linux-tarball` as build args; the alpine toolchain is
pinned by digest on its own `FROM` line, since a pin consumed by exactly one
stage does not need routing through every target. Bumping the kernel means
changing both variables together — a new version against the old checksum just
fails the `ADD`.

## Build and run

```sh
docker buildx bake kernel   # vmlinuz + kernel.config + System.map -> out/
docker buildx bake config   # what Kconfig resolved, WITHOUT compiling — fast
docker buildx bake uapi     # kernel uapi headers -> out/uapi (uapi-x86_64 likewise)
./src.sh                    # whole kernel tree -> out/src/ (opt-in, ~1.7 GB, ~40s)

meson setup .cache/meson-aarch64 --cross-file cross/aarch64.ini -Dtests=true -Dubsan=enabled
meson compile -C .cache/meson-aarch64      # the check that matters while editing
meson test    -C .cache/meson-aarch64      # program-header contract on the probes
ninja -C .cache/meson-aarch64 clang-tidy   # from meson's own compile database

./run.sh                    # boot (QEMU + hvf, 1 vCPU). Ctrl-C quits
ACCEL=tcg SMP=4 ./run.sh    # multi-core — slow, but hvf cannot do SMP
./debug.sh                  # same boot, halted, gdbstub on :1234
lldb -o 'gdb-remote localhost:1234'
```

**`meson compile` is the check that matters while editing.** libuc builds
under `-Weverything -Werror` with minimal UBSan, and each architecture has its
own build directory — compile both `.cache/meson-aarch64` and
`.cache/meson-x86_64` before claiming the tree clean. clang-tidy runs as
meson's built-in target from the same compile database the build uses, and the
root `compile_commands.json` symlinks into `.cache/meson-aarch64`, so the
editor, the build, and the tidy checks cannot drift apart. **A green
`docker buildx bake kernel` says nothing about libuc** — the bake builds only
the kernel.

`bake config` is the fast path for any "is X enabled?" question — it resolves
Kconfig without a compile. Kconfig turns on far more than the fragment asks for,
so **`out/kernel.config` is the authority on what a build actually is**;
`build/kernel.config` only records what was requested.

`debug.sh` delegates to `run.sh`, adding only `-s -S`. Keep it that way: the two
must not drift, least of all on the console device.

## The VM has three sharp edges

Each cost a debugging session to find; all are measured, not assumed.

1. **The console is `hvc0`, never `ttyAMA0`.** `CONFIG_SERIAL_AMBA_PL011 is not
   set`, so `-nographic` — which binds the console to PL011 — boots completely
   silently with no error from QEMU. The console comes from an explicit
   `virtio-serial-device` + `virtconsole` pair. (Adding
   `CONFIG_SERIAL_AMBA_PL011=y` would also unlock `earlycon`, which
   virtio-console cannot provide since it needs the device probed first.)
2. **hvf hangs with more than one vCPU.** QEMU 11.1.0, every gic-version, zero
   output, no error. Not a kernel limit — `CONFIG_SMP=y`, `NR_CPUS=512`, and
   TCG boots `-smp 4` fine. Milestone 3 must choose TCG, or bring vfkit back as
   a second launcher, or be on bare metal by then.
3. **No kernel symbols in lldb.** `out/vmlinuz` is a raw stripped `Image`, so
   symbolic breakpoints do not work; registers, disassembly, stepping and memory
   do. The runtime's own ELF (`-g -static`) will symbolise fine. Export
   `/linux/vmlinux` if kernel-side debugging is ever needed.

## Current boot state

`./run.sh` boots libuc's startup probe as PID 1:
`meson compile -C .cache/meson-aarch64 initramfs` wraps it, and run.sh's
INITRD default points at the result (`INITRD=` overrides it;
`initramfs-no-thread-local` wraps the probe without a PT_TLS segment). The
probe exits, so the boot ends in the kernel's Attempted-to-kill-init panic
with the probe's status in `exitcode=` — that panic line is today's
acceptance signal, and it stops being acceptable the day a resident runtime
becomes PID 1.

Do not reintroduce busybox or a shell without discussion — the design calls for
the runtime to be PID 1.

## C conventions

**Clang builds the runtime. GCC builds the kernel.** Not a style preference —
three things depend on it: BPF is Clang-only in practice, and the in-kernel loop
(`loop.c`/`bpf-ops.c`) is a retained direction; `lldb` is the only debugger on
this machine and pairs with Clang's debug info; and Clang's freestanding UBSan
has no GCC equivalent. The kernel stays on GCC because it builds today and
switching is orthogonal risk — the runtime shares no code with it, so toolchain
consistency buys nothing.

Verified on the pinned Alpine: Clang 22.1.3, GCC 15.2.0, both fully adequate for
the C23 this project uses.

Build flags are load-bearing, not stylistic:

```
-std=c23 -ffreestanding -nostdlibinc -fno-stack-protector -fno-omit-frame-pointer
-g -O2 -Weverything -Werror
-fsanitize=undefined,local-bounds -fsanitize-minimal-runtime -fno-sanitize-recover=all
```

with the probes linked `-nostdlib -nostartfiles -static` against `libuc.ld`.

- `-nostdlibinc` — drops the platform's libc include paths but keeps clang's
  own, so `<stdint.h>` survives while `<stdio.h>` cannot be reached. Invariant
  4 enforced by the compiler instead of by discipline.
- `-static` — fixed load addresses; without them the gdbstub is a guessing
  game.
- `-fno-omit-frame-pointer` — backtraces walk the FP chain.
- `-fno-stack-protector` — otherwise `__stack_chk_fail` is undefined.
- `-fsanitize=undefined -fsanitize-minimal-runtime` — the sanitizer half of
  the test story, dev-only: the shipped build is plain `-O2`. Works
  freestanding: it emits undefined `__ubsan_handle_*_minimal_abort` symbols
  that `src/ubsan.c` implements. Catches shifts past width, misaligned loads,
  and signed overflow in exactly the hand-rolled pointer arithmetic where
  there is no other safety net.

Rules:

- **`static_assert` assumptions the types cannot express**, not struct layouts —
  those come from the kernel's headers and are never retyped. The canonical
  case: `sizeof(struct io_uring_cqe)` is 16 even under `IORING_SETUP_CQE32`
  (`big_cqe[]` is a flexible array), so ring stride follows the setup flags, not
  the type. Assert the flags, not the size.
- **No `errno`.** Raw syscalls return `-errno` in `-1..-4095`.
- **`memcpy`/`memset`/`memmove`/`memcmp` must exist.** The compiler emits calls
  to them even under `-ffreestanding`.
- **Ring head/tail use acquire/release**, not plain loads and stores. Plain
  accesses work on x86 and fail on aarch64, which is the target.
- Prefer C23: `constexpr`, `nullptr`, `auto`, `[[nodiscard]]`, keyword
  `static_assert`, binary literals.

## Testing

Two tiers. `meson test` is the mechanical one: it checks the probes' ELF
contract (program headers via `llvm-readelf`) on every host, and runs the
probes themselves wherever the machine can execute them — layer
`cross/runnable.ini` after the architecture file in a matching Linux
environment; on this Mac they cross-compile but do not run.

Behaviour beyond what a host can see is checked **in-VM, by console
inspection**. Each ticket in `.scratch/tickets/` carries explicit acceptance
criteria; running them is the test. Boot the probe's initramfs, read the
console, compare. Keep acceptance criteria mechanically checkable — an exact
expected console string, not "looks right".

The cost, stated plainly: **the in-VM half has no regression net.** Nothing
catches a change that breaks an earlier ticket's boot behaviour, so when
touching shared code (`start.c`, `src/thread_local/`, `src/arch/`), re-run the
acceptance checks of every ticket that depends on it, not just the current
one. Revisit when either happens: a regression escapes twice, or the manual
checks stop fitting in one console screen.

## Where work is tracked

`.scratch/plan.md` for decisions and rationale; `.scratch/tickets/uc/UC-00N-*.md`
for the active work items with acceptance criteria (the `RT-00N` tickets
belonged to the retired spike, closed). Read the plan before proposing
architecture — most of it
has already been argued through, and the rationale for rejected alternatives
is recorded there.
