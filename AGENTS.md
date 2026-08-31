# AGENTS.md

## Project

libuc is a freestanding C23 libc for 64-bit little-endian Linux. Potentially
blocking C/POSIX calls suspend the current cooperative fiber over io_uring.
There is no host libc and no liburing; ring mechanics and context switching are
implemented here.

Linux 7.2, PID 1, and the minimal QEMU image are the development scaffold. The
product is the libc, its scheduler-per-task ownership model, and the ring as its
syscall ABI. The future language is deliberately deferred.

## Sources of truth

- `out/src/` is the complete pinned Linux 7.2 tree and the authority for
  opcodes, flags, layouts, and kernel behavior. Read it rather than recalling
  older kernels or using man pages; cite file and line for kernel claims.
- If `out/src/` is absent, regenerate it with `./src.sh`. Never edit `out/`.
- `out/kernel.config` is the resolved configuration; `build/kernel.config` is
  only the requested fragment.
- `meson.build` and the per-directory Meson files are the build authority.
- `.scratch/plan.md` records cross-ticket architecture. Acceptance criteria and
  status live in `.scratch/tickets/uc/`.

Read the plan before proposing architecture. Linux 7.2 includes facilities
older references omit, including `IORING_OP_BIND`/`LISTEN`,
`IORING_SETUP_SQ_REWIND`, capability probing in `query.c`, and the BPF
`struct_ops` loop in `io_uring/loop.c` and `bpf-ops.c`.

## Invariants

Do not violate these without explicit discussion.

1. **io_uring is the syscall ABI.** If an operation has an opcode, it goes
   through the ring. Direct syscalls are limited to operations without one:
   `mmap`, `mprotect`, `munmap`, `clone`, `sched_setaffinity`,
   `rt_sigaction`, `sigaltstack`, `io_uring_*`, and `exit_group`, plus the one
   registered diagnostic exception below.

2. **Never use `IORING_SETUP_SQPOLL`.** Rings use
   `SINGLE_ISSUER | DEFER_TASKRUN | NO_SQARRAY | SQ_REWIND | SUBMIT_ALL`.
   SQPOLL conflicts with DEFER/COOP task-run modes
   (`out/src/io_uring/io_uring.c:2815-2821`).

3. **Shared-nothing; fibers never migrate.** A fiber is created, scheduled,
   and destroyed by one scheduler. No work stealing and no cross-scheduler
   free. Per-scheduler state needs no atomics unless it is explicitly shared.
   The ownership unit is the scheduler task, not a CPU: the kernel binds a ring
   to `current` (`out/src/io_uring/io_uring.c:3065-3067`).

4. **Do not consume or link another libc.** Runtime code builds with
   `-ffreestanding -nostdlibinc`; probes link with `-nostdlib -nostartfiles`.
   Permitted header definitions come from only:

   - Clang's freestanding headers: `<stdint.h>`, `<stddef.h>`, `<stdarg.h>`,
     `<stdatomic.h>`, and `<stdckdint.h>`. Do not use `<stdbit.h>`; use the
     compiler bit-count builtins.
   - The pinned kernel's installed UAPI headers. Never locally reproduce a
     definition UAPI already provides.
   - Libc-authored ABI types/constants where UAPI has no public definition,
     such as `socklen_t`, `struct sockaddr`, `AF_*`, and `SOCK_*`. Add only
     what a public call consumes, verify values against `out/src/`, and record
     each addition in its ticket.

5. **Do not add liburing.** Hand-written ring mechanics are intentional.

6. **A resident PID 1 must not exit.** Returning acceptance probes may
   deliberately reach `exit_group`; a resident runtime must loop or reboot.

7. **Scheduling is cooperative.** Fibers yield only at runtime I/O points or
   through the private `__libuc_fiber_yield()` implementation.

   A call that reaches the scheduler is such a point: `thrd_create` may
   reschedule, and so may anything built on a fiber request. What follows
   is the runtime's choice wherever no standard fixes it, and keeping it
   unpromised is what leaves fairness and locality free to change. Where
   a standard does fix it, the runtime obeys: `thrd_create`'s completion
   synchronizes with the start of the new thread (C11 7.26.5.1), so a
   spawner is resumed far enough to store the handle before its child
   runs.

8. **Do not create another purity exception silently.** Update the registry
   and discuss it first.

### Purity exception registry

| symbol | status | exception |
|---|---|---|
| `raw_write()` | approved, not implemented | Direct `write(2)` reserved for pre-ring setup failure and a future crash handler where the ring may be corrupt. |

## Code placement and C rules

- An implementation directory mirrors its installed header path without `.h`:
  `<string.h>` maps to `src/string/`, `<sys/auxv.h>` to `src/sys/auxv/`.
  Runtime-only machinery such as `src/fiber/`, `src/ring/`, and
  `src/scheduler/` has no installed header. A constants-only header needs no
  translation unit or directory.
- Architecture selection belongs to the include path. Generic code includes
  names such as `syscall_arch.h`; Meson selects `src/arch/<arch>/`. Never name
  `arch/aarch64` or `arch/x86_64` from generic code or introduce an architecture
  `#ifdef` ladder.
- Clang and LLD build libuc; GCC builds the kernel. Do not switch or relax the
  warning/sanitizer contract without discussion. The actual flags live in
  `meson.build`.
- Raw syscall wrappers return Linux results (`-errno` in `-1..-4095`) and never
  touch public `errno`. Public libc calls translate at their boundary; `errno`
  is compiler-visible `_Thread_local` state and therefore per fiber.
- Ring head/tail publication uses acquire/release operations. Plain accesses
  that happen to work on x86 are wrong for AArch64.
- The compiler may emit `memcpy`, `memset`, `memmove`, and `memcmp` even when
  freestanding; libuc must continue to provide them.
- Use `static_assert` for assumptions the types cannot express. Do not assert a
  retyped kernel layout. In particular, CQE stride follows setup flags rather
  than `sizeof(struct io_uring_cqe)`.
- Prefer C23 (`constexpr`, `nullptr`, `auto`, `[[nodiscard]]`, keyword
  `static_assert`, binary literals).

## Build and verification

Generate the UAPI exports when missing:

```sh
docker buildx bake uapi
```

`uapi` and `libuc` build both platforms in one bake, which the default
docker driver refuses; once per machine, create the container-driver
builder: `docker buildx create --use`.

Configure the two development builds when missing:

```sh
meson setup .cache/meson-aarch64 --cross-file cross/aarch64.ini -Dtests=true -Dubsan=enabled
meson setup .cache/meson-x86_64 --cross-file cross/x86_64.ini -Dtests=true -Dubsan=enabled
```

Before claiming the tree clean, run:

```sh
meson compile -C .cache/meson-aarch64
meson compile -C .cache/meson-x86_64
meson test -C .cache/meson-aarch64 --print-errorlogs
meson test -C .cache/meson-x86_64 --print-errorlogs
ninja -C .cache/meson-aarch64 clang-tidy
```

`meson test` checks the probes' ELF contract everywhere and executes probes only
where the host/cross configuration permits it. Behavioral acceptance is the
ticket's in-VM console check. When shared startup, TLS, fiber, scheduler, ring,
or architecture code changes, rerun every dependent ticket's probe.

`docker buildx bake libuc` reproduces the CI build exactly: the same check
and release meson runs plus clang-tidy for both platforms, inside the
toolchain nix realizes from the devshell's own `flake.lock`
(`build/libuc.Dockerfile`), exporting the release tarballs to
`out/libuc/linux_<arch>/`. CI layers `docker-bake.gha-cache.hcl` on top for
its per-target layer cache; local builds never need it.

A kernel bake does not validate libuc. Use `out/kernel.config`, not the fragment,
when answering configuration questions.

## VM constraints

- Use `./run.sh`; `debug.sh` must remain a thin `-s -S` wrapper around it.
- The console is `hvc0` through virtio-console. PL011/`ttyAMA0` is disabled, so
  `-nographic` produces a silent boot.
- HVF is single-vCPU in this setup. Use `ACCEL=tcg SMP=4 ./run.sh` for SMP.
- Do not add BusyBox or a shell without discussion; the probe/runtime is PID 1.
