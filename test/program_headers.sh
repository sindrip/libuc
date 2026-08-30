#!/bin/sh
set -eu

readelf=$1
without_thread_local=$2
shift 2

thread_local_count() {
  "$readelf" --program-headers --wide "$1" |
    awk '$1 == "TLS" { count++ } END { print count + 0 }'
}

expect_thread_local_count() {
  executable=$1
  expected=$2
  actual=$(thread_local_count "$executable")

  if [ "$actual" -ne "$expected" ]; then
    printf '%s: expected %s PT_TLS entries, found %s\n' \
      "$executable" "$expected" "$actual" >&2
    return 1
  fi
}

expect_thread_local_count "$without_thread_local" 0

# Every remaining argument is a probe that declares thread-local data.
for with_thread_local in "$@"; do
  expect_thread_local_count "$with_thread_local" 1
done
