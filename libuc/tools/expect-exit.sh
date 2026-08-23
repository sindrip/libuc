#!/bin/sh
set -u

expected=$1
program=$2
shift 2

# Meson exposes a cross file's executable wrapper here. Word splitting is
# intentional: wrapper arguments precede the target executable.
if [ -n "${MESON_EXE_WRAPPER:-}" ]; then
  set -f
  # shellcheck disable=SC2086
  set -- $MESON_EXE_WRAPPER "$program" "$@"
  "$@"
else
  "$program" "$@"
fi
actual=$?

if [ "$actual" -ne "$expected" ]; then
  printf 'expected exit status %s, got %s\n' "$expected" "$actual" >&2
  exit 1
fi
