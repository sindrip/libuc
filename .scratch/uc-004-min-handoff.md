# UC-004-min handoff

The staged path keeps the TCB while deferring compiler-visible TLS:

```text
UC-004-min: TCB-only block allocation/destruction      [done]
UC-005:     thread-pointer installation                [done]
UC-006-min: TCB/fiber binding during switches          [done]
UC-007:     suspension/resumption                      [done]
later:      PT_TLS image and _Thread_local data        [absorbed: image by
            UC-004-min, _Thread_local by UC-005..007 and the UC-006 close]
```

This preserves the important design—the TCB is present and fiber-owned—while
postponing only the static TLS payload.

UC-004-min now allocates an architecture-correct runtime block, initializes the
TCB self-pointer and null fiber pointer, supports PT_TLS image/zero-fill when
the executable declares one, and destroys independent blocks. It does not
install the thread pointer. An executable without `PT_TLS` still receives a
TCB-only block.

Next work is UC-005: install a block's recorded thread-pointer value without
allocating or changing block ownership. UC-006-min can then attach each block
to its fiber and carry the TCB through context switches, giving UC-007 the
current-fiber lookup it needs without requiring a compiler `_Thread_local`
variable yet.

Validation: both architectures compile, link, and pass clang-tidy; there is
no toolchain blocker. Clang 22.1.8 accepts `__builtin_bswapg`, so
`src/string/memcmp.c` builds — an earlier note here claimed otherwise and was
wrong.

The staged acceptance passes. Probes run as containers per
`tickets/findings.md`, UBSan armed: `thread-local-block`,
`start-no-thread-local`, `start`, and `fiber` all exit 0 on aarch64 natively
and on x86-64 under `--platform linux/amd64`. Booted on aarch64,
`thread-local-block` panics init with `exitcode=0x00000000`.

Because `meson test` gates probe execution behind `can_run_host_binaries()`,
neither container nor VM runs happen on a macOS host. The ungated coverage is
the program-header contract, which now includes this probe, and the
`initramfs-thread-local-block` target, which makes the boot repeatable.

The shared-code regression sweep AGENTS.md asks for is green: `start`,
`start-no-thread-local`, and `fiber` boot to `exitcode=0x00000000`, and
`exit-status` to `0x00002a00`.

## UC-005 (2026-08-30)

Landed: two operations. `__libuc_thread_local_install_available` is the
init-time capability question — the AT_HWCAP2 walk and, on x86-64, the
FSGSBASE test, failing closed rather than admitting arch_prctl to the
direct-syscall whitelist. `__libuc_thread_local_block_install` takes the
handle and is the bare register write (`msr tpidr_el0` / `wrfsbase`), zero
checks: the caller confirmed availability at initialization, and violating
that traps loudly. UC-006's switch path therefore calls install directly.
The probe alternates two blocks under one `_Thread_local` variable through
noinline accessors (within a frame the compiler may cache a TP-relative
address across the installs).

Acceptance: aarch64 exit 0 in container and VM; disassembly confirms
compiled offsets meet the placement (TP+0x10 variant 1, %fs:-0x4 variant 2).
x86-64 exits 125 under emulation — no `HWCAP2_FSGSBASE` — so the gate is
verified and `wrfsbase` itself awaits real x86-64 hardware. Full regression
sweep green on both architectures.

## UC-006-min (2026-08-30)

Landed: the fiber owns `thread_local_block` by value; create binds
`tcb->fiber`, run saves/installs/restores the thread pointer around the
switch, `__libuc_fiber_current` reads the fiber out of the installed TCB.
UC-003's root fiber means `main` already runs with its own block installed —
most of full UC-006 arrived with the embedding.

Acceptance: aarch64 green everywhere — fiber-thread-local, fiber,
no-thread-local, install, start all exit 0 in containers, and the three
fiber-affected probes boot to `exitcode=0x00000000`. x86-64: startup now
installs before any gate, so probes hang under Rosetta (wrfsbase neither
works nor traps cleanly there); emulated x86-64 behavioral runs are retired,
compile-and-link stays. The decision and its cost are recorded in the
UC-006 ticket.

## UC-007 (2026-08-30)

Landed: resume/yield replace the one-shot run. `__libuc_fiber_resume`
plants NONE, switches with the thread pointer carried, poisons the context
on EXIT, and returns the request; `__libuc_fiber_yield` writes YIELD through
the current-fiber TCB word and switches to `return_to`. `start.c` resumes
the root fiber once and treats anything but EXIT as broken. The request
enum sets the house idiom: lowercase tag, SCREAMING members, width fixed to
the struct slot.

Acceptance green on aarch64: all seven probes exit clean in containers and
VM, exit-status still carries 42. The register harness now saves and
restores callee-saved registers around its yield — a fiber that suspends
and later completes owes its C frames the ABI.

UC-006 is closed in full: `test/main.c` asserts constructor- and
main-visible root-fiber TLS.

## UC-008 (2026-08-30)

Landed: `src/ring/` with create/append_sqe/submit/reap over five setup
flags — the invariant three plus 7.2's SQ_REWIND and SUBMIT_ALL, adopted
after auditing every bit of IORING_SETUP_FLAGS (ledger in the ticket).
REWIND deletes the SQ head/tail protocol entirely: batches write from slot
zero, enter consumes them synchronously, and the only memory ordering left
is the CQ acquire/release pair. NOP acceptance green in container and VM.
Next: UC-009, scheduler-become.
