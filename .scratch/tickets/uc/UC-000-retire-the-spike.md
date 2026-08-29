---
id: UC-000
title: Retire the spike
status: next
depends: []
---

## Goal

Remove the pre-libuc runtime now that libuc boots its own probes.

## Spec

Delete `spike/`. Drop the runtime-build stage and its initramfs export from
`build/kernel.Dockerfile`, so the kernel bake stops producing
`out/initramfs.cpio.gz`. Point `run.sh`'s INITRD default at the meson
`initramfs` target's output, failing with a message that names the target
when it has not been built. Remove the spike from AGENTS.md.

## Files

- `spike/`
- `build/kernel.Dockerfile`
- `run.sh`
- `AGENTS.md`

## Acceptance

`docker buildx bake kernel` exports vmlinuz, config, and System.map but no
initramfs; after `meson compile -C .cache/meson-aarch64 initramfs`,
`./run.sh` boots the startup probe as PID 1; nothing under `spike/` remains.
