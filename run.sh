#!/usr/bin/env /bin/sh
# Boot out/vmlinuz under QEMU. With no rootfs the kernel comes up, prints its
# log, and panics on "Unable to mount root fs". Ctrl-C quits.
#
# Extra arguments pass through to qemu; debug.sh uses that to add -s -S, so the
# two launchers cannot drift apart.
set -eu
cd "$(dirname "$0")"

# hvf is fast but hangs with more than one vCPU — measured on qemu 11.1.0, at
# every gic-version, producing no console output at all. tcg is slow but does
# SMP correctly, so multi-core work overrides both:
#
#   ACCEL=tcg SMP=4 ./run.sh
#
# -cpu host is hvf-only; tcg rejects it (and offers only cortex-a53/a57/max).
ACCEL="${ACCEL:-hvf}"
SMP="${SMP:-1}"

# What boots as PID 1. The default is the runtime's image; libuc's Makefile
# wraps its test ELFs the same way (INITRD=out/libuc/aarch64/<name>.initramfs.cpio.gz).
INITRD="${INITRD:-out/initramfs.cpio.gz}"
case "$ACCEL" in
  hvf) CPU=host ;;
  *)   CPU=max  ;;
esac

# console=hvc0, not ttyAMA0: this kernel has VIRTIO_CONSOLE=y but
# SERIAL_AMBA_PL011 is not set, so a PL011 console is silent. That also rules
# out -nographic, which binds the console to the PL011 that does not exist.
#
# QEMU's user network gives the guest DHCP and forwards host TCP port 8080 to
# the same guest port. Bind the host side to loopback: this is a development
# server, not something that should appear on the local network.
exec qemu-system-aarch64 \
  -machine virt \
  -accel "$ACCEL" -cpu "$CPU" \
  -m 2G -smp "$SMP" \
  -kernel out/vmlinuz \
  -initrd "$INITRD" \
  -append "console=hvc0 ip=dhcp" \
  -display none -serial none -no-reboot \
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:8080-:8080 \
  -device virtio-net-device,netdev=net0 \
  -device virtio-serial-device \
  -chardev stdio,id=con0 \
  -device virtconsole,chardev=con0 \
  "$@"
