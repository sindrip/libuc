# UC-004-min handoff

The staged path keeps the TCB while deferring compiler-visible TLS:

```text
UC-004-min: TCB-only block allocation/destruction
UC-005:     thread-pointer installation
UC-006-min: TCB/fiber binding during switches
UC-007:     suspension/resumption
later:      PT_TLS image and _Thread_local data
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
