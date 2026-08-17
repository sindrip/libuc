---
id: RT-002
title: debug.sh — boot under QEMU with a gdbstub
status: done
depends: []
---

## Goal

Have a working debugger *before* writing any assembly.

## Outcome

`run.sh` moved from vfkit to QEMU, and `debug.sh` delegates to it — adding only
`-s -S`, so machine, cpu, memory and console can never drift between the two.

Verified end to end: lldb attaches, halts at `0x40000000` (the arm64 `Image`
entry point), disassembles the boot prologue, reads registers, and `stepi`
advances the PC.

```sh
./debug.sh &
lldb -o 'gdb-remote localhost:1234'
```

## What the verification actually found

Three things, each of which would have cost a debugging session later.

**1. `-nographic` is unusable with this kernel.** It binds the console to the
PL011 UART, and `CONFIG_SERIAL_AMBA_PL011 is not set` — so `ttyAMA0` does not
exist and the boot is completely silent, with no error from QEMU. The kernel has
`VIRTIO_CONSOLE=y` and `HVC_DRIVER=y`, so the console is **`hvc0`**, reached via
an explicit device pair:

```
-display none -serial none
-device virtio-serial-device
-chardev stdio,id=con0
-device virtconsole,chardev=con0
```

If a future kernel config adds `CONFIG_SERIAL_AMBA_PL011=y`, `-nographic`
becomes viable and brings `earlycon` with it — output from before console
registration, which virtio-console cannot provide because it needs the device
probed first. Worth doing if early-boot debugging ever gets painful.

**2. hvf hangs with more than one vCPU.** Measured on QEMU 11.1.0: `-smp 1`
boots; `-smp 2` and `-smp 4` produce zero console output at every gic-version,
with no error. Not a kernel limitation — `CONFIG_SMP=y`, `CONFIG_NR_CPUS=512`,
and the same kernel boots `-smp 4` fine under TCG. So:

| | accel | cores | speed | debugger |
|---|---|---|---|---|
| milestones 1–2 | hvf | 1 | fast | ✅ |
| milestone 3 | tcg (`ACCEL=tcg SMP=4`) | N | slow | ✅ |
| milestone 3, alt | vfkit | N | fast | ❌ |

`run.sh` defaults to hvf/1 and takes `ACCEL` and `SMP` from the environment.
**Milestone 3 needs a decision here** — TCG for correctness work, or vfkit
returns as a second launcher, or bare metal arrives first.

Also note `-cpu host` is hvf-only; TCG offers `cortex-a53`, `cortex-a57`, `max`.
`run.sh` picks the CPU from `ACCEL` for that reason.

**3. No kernel symbols.** lldb reports `Target 0: (No executable module.)`
because `out/vmlinuz` is a raw stripped `Image`, not ELF. Registers,
disassembly, stepping and memory all work; symbolic breakpoints like
`b start_kernel` do not.

This does not block the project — the target is the *runtime*, and its ELF built
`-g -static -no-pie` will resolve fine. If kernel-side debugging is ever wanted,
export `/linux/vmlinux` from the `kernel-build` stage alongside `Image`.

## Files

- `run.sh` — QEMU launcher, `ACCEL`/`SMP` overridable, args passed through
- `debug.sh` — `exec ./run.sh -s -S "$@"`

## Acceptance — all met

- `./run.sh` boots and reaches
  `Kernel panic - not syncing: VFS: Unable to mount root fs` — the expected
  no-rootfs state.
- `./debug.sh` halts at reset: gdbstub listening on :1234, zero console output.
- lldb connects, reads `pc`/`sp`/`x0`, and `stepi` moves `pc`
  `0x40000000` → `0x40000004`.
- Both launchers share one console device by construction.
