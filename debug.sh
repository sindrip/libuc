#!/usr/bin/env /bin/sh
# Exactly run.sh's boot, halted at reset with a gdbstub on :1234.
#
#   ./debug.sh &
#   lldb -o 'gdb-remote localhost:1234' -o 'b start_kernel' -o continue
#
# Delegating to run.sh is deliberate: machine, cpu, memory and — above all — the
# console device stay identical, so a symptom seen here is the same symptom seen
# there. out/System.map resolves kernel symbols; -static -no-pie will resolve the
# runtime's once it exists.
set -eu
cd "$(dirname "$0")"
exec ./run.sh -s -S "$@"
