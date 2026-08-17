---
id: RT-003
title: Freestanding skeleton — _start, raw syscalls, PID 1
status: done
depends: [RT-001]
---

## Goal

A `-ffreestanding -nostdlib` aarch64 binary that boots as PID 1, proves it is
alive on the console, and does not panic. No ring, no coroutines.

## Outcome

```
Run /init as init process
rt: alive
```

Everything below records what was built, not what was originally specced; where
the two diverged the reason is noted.

## What it took

### Syscall numbers come from `<asm/unistd.h>`, never transcribed

`make ARCH=arm64 headers` in a `uapi-build` stage, exported to `out/uapi/include`
and mounted at `/uapi/include` in the runtime build.

**Not `headers_install`**, as originally specced: its final step rsyncs
`usr/include` into `INSTALL_HDR_PATH`, and rsync is not in the toolchain image.
Adding it would have invalidated the toolchain layer and forced a full kernel
rebuild. `make headers` produces the identical tree at `usr/include`.

This matters beyond tidiness: `include/uapi/asm-generic/unistd.h:891` defines
`__NR_mmap` as `__NR3264_mmap`, resolved through `__SC_3264` and a
`__BITS_PER_LONG` branch. Hand-copying `222` means hand-evaluating a
preprocessor conditional and hoping.

### `src/syscall.h`

`sys1`, `sys3` — inline asm, number in `x8`, args `x0`–`x5`, `svc #0`, result in
`x0`. Only `x0` needs a read-write constraint: verified at
`out/src/arch/arm64/kernel/syscall.c:54` that `invoke_syscall` writes only
`regs[0]`, so `x1`–`x7` survive the trap and need no clobber.

`"memory"` is load-bearing — without it the compiler may keep the write buffer
in registers and never store it, so the kernel reads stale memory. Works at
`-O0`, breaks at `-O1`.

`sys_failed(r)` is `r < 0 && r >= -4095`. The range matters: `mmap` returns
addresses with the top bit set, so `r < 0` alone misreads every high mapping.

`raw_write(int fd, const void *buf, unsigned long len)` — gained an `fd`
parameter over the original spec. Pointer conversion goes `(long)(uintptr_t)buf`;
`(long)len` is explicit because `-Wsign-conversion` correctly flags the implicit
`unsigned long` → `long`.

### `src/start.S`

```
mov x29, xzr        // terminate the FP chain for RT-007's walk
mov x30, xzr
mov x0, sp          // capture BEFORE aligning; rt_main's argument
and sp, x0, #-16    // round down; the stack grows down
bl  rt_main         // bl not b, so the fallback below is reachable
mov x8, __NR_exit_group
mov x0, 127
svc #0
```

`#-16` rather than `#~15` or `bic #15`: all three encode identically to
`and sp, x0, #0xfffffffffffffff0`, and `#-16` is what musl's aarch64 `_start`
uses. The immediate form of `AND` accepts `sp` as a destination; the
shifted-register form does not.

Status `127` is deliberate. The kernel prints it —
`kernel/exit.c:1163` does `(error_code & 0xff) << 8`, and `exit.c:963` panics
with `exitcode=0x%08x` — so this path announces itself as `exitcode=0x00007f00`,
which nothing else in the system can produce. `1` would render as
`0x00000100`, indistinguishable from any generic failure.

### `src/main.c`

`static const char banner[]`, not `const char *`. A string literal assigned to a
pointer loses its array type, so `sizeof` stops being the length — the first
attempt used `sizeof(*banner) - 1`, which is `0`, and the compiler folded the
length argument to `xzr`. It compiled clean under `-Wall -Wextra -pedantic
-Wsign-conversion` and wrote nothing. Only the disassembly showed it.

`static` specifically: a non-static local array is constructed per call and the
compiler may emit a `memcpy` to do it — a symbol that does not exist here.

Idle is `for (;;) { __asm__ volatile("wfe"); }`. A side-effect-free infinite loop
is UB in C11+ and may be deleted; `wfe` also parks the core rather than spinning.

### Build and image

`runtime-toolchain` (clang, lld, clang-extra-tools, cpio, gzip) → `runtime-build`
compiles and links `src/`, then `find . | cpio -o -H newc | gzip -9` from inside
`/rootfs` so paths are relative. `kernel` exports `initramfs.cpio.gz` plus
`rt.elf` — the unpacked binary, for debugging symbols.

**`-no-pie` was dropped**: `-static` already implies it (verified, ELF
`Type=EXEC` and identical entry point either way), and clang errors under
`-Werror` that the argument went unused.

## Acceptance

- ✅ `docker buildx bake kernel` produces an image whose `/init` is the runtime.
- ✅ `./run.sh` prints `rt: alive` after `Run /init as init process`.
- ✅ No panic — PID 1 stayed alive in the idle loop.
- ✅ `lldb out/rt.elf` + `gdb-remote localhost:1234` resolves `b rt_main` to
  `0x2101a8`, with line info (`rt_main [inlined] sys3 at syscall.h:61`).
  **Symbol resolution only** — the breakpoint did not fire. QEMU's gdbstub is
  CPU-level with no process awareness, so a userspace virtual address is only
  meaningful once that address space is mapped. Breaking inside userspace needs
  a different approach; RT-004 will need it.

## Deferred out of this ticket

- **`src/string.c` — not written, and not needed.** The spec called `memcpy`,
  `memset`, `memmove` and `memcmp` mandatory. At this size clang synthesised
  none of them; `nm -u` shows zero undefined symbols. The hazard is real but
  arrives with larger structs and array initialisation, so this returns when a
  link actually fails for it.
- **UBSan is not wired.** `-fsanitize=undefined -fsanitize-minimal-runtime
  -fno-sanitize-recover=all` is in neither the host build (Apple clang has no
  minimal runtime) nor `runtime-build`. It needs RT-007's
  `__ubsan_handle_*_minimal_abort` handlers to link, so it belongs with that
  ticket rather than here.
- **`docker-bake.hcl` has no comment describing the `kernel` target's outputs.**
