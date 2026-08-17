#!/usr/bin/env /bin/sh
# Export the pinned kernel tree to out/src — the read-only ground truth
# AGENTS.md points at. Opt-in: ~1.7 GB, ~95k files, about 40s.
#
# Why a script rather than `docker buildx bake src` on its own:
#
#   The cost here is file count, not bytes. type=tar streams the stage as one
#   blob and lets the host untar it natively — 19s to send, 21s to unpack,
#   measured. type=local walks the same 95k files over the fsutil protocol one
#   at a time across the docker socket and is far slower; the per-file protocol
#   is the likely cause, but that half was never measured.
#
#   And no exporter prunes: it overwrites files but never deletes ones absent
#   from the stage. Clearing out/src first is what stops a KERNEL_VERSION bump
#   leaving files deleted upstream behind in the directory whose whole job is
#   being accurate.
set -eu
cd "$(dirname "$0")"

TAR=out/linux-src.tar
trap 'rm -f "$TAR"' EXIT

docker buildx bake src
rm -rf out/src
mkdir -p out/src
tar xf "$TAR" -C out/src

# What actually landed, not what was asked for.
grep -m3 -E '^(VERSION|PATCHLEVEL|SUBLEVEL)' out/src/Makefile | tr '\n' ' '
echo
