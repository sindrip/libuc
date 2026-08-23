#!/bin/sh
set -eu

input=$1
output=$2
root="${output}.root"

cleanup() {
  rm -rf "$root"
}
trap cleanup EXIT HUP INT TERM

cleanup
mkdir -p "$root"
cp "$input" "$root/init"
(
  cd "$root"
  printf '%s\n' init | cpio -o -H newc
) | gzip -n >"$output"
