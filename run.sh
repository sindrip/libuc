#!/usr/bin/env /bin/sh
# Boot out/vmlinuz under Apple Virtualization.framework, with no rootfs: the
# kernel comes up, prints its log, and panics for want of an init. Ctrl-C quits.
set -eu
cd "$(dirname "$0")"
exec vfkit \
  --bootloader "linux,kernel=$PWD/out/vmlinuz,cmdline=\"console=hvc0\"" \
  --device virtio-serial,stdio
