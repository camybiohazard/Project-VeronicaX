#!/usr/bin/env bash
# Run under gdb with debug logging, for diagnosing hangs.
# When the game hangs, press Ctrl+C in this terminal: every thread's stack is
# written to notes/logs/hang-backtrace.txt and the game is closed. A crash or
# trap (e.g. "Switch case out of range" -> SIGILL), a guest null-pointer
# access, or the game raising its disc read error does the same by itself;
# the top of the file then says what stopped it and on which thread.
# Usage: scripts/debug-run.sh [preset] [extra rexglue flags...]
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

preset="${1:-dev-relwithdebinfo}"
shift || true

bin="out/build/$preset/veronicax"
if [ ! -x "$bin" ]; then
    echo "$bin not found; run scripts/build.sh first" >&2
    exit 1
fi

mkdir -p notes/logs
out="$root/notes/logs/hang-backtrace.txt"
rm -f "$out"

# The runtime uses SIGSEGV for guest memory tracking, so let it through,
# except for faults in the first 64 KB of guest memory (host 0x100000000):
# those are guest null-pointer accesses ("Unhandled guest access violation:
# write of guest 0x00000000"), and stopping there shows the guilty code.
gdb -q -batch \
    -ex "set pagination off" \
    -ex "set debuginfod enabled off" \
    -ex "handle SIGSEGV SIGBUS SIGPIPE SIG32 SIG33 SIG34 SIG35 SIG36 nostop noprint pass" \
    -ex "catch signal SIGSEGV" \
    -ex 'condition $bpnum (unsigned long)$_siginfo._sifields._sigfault.si_addr - 0x100000000 < 0x10000' \
    -ex "set breakpoint pending on" \
    -ex "break __imp__XamShowDirtyDiscErrorUI" \
    -ex "run" \
    -ex "set logging file $out" \
    -ex "set logging overwrite on" \
    -ex "set logging redirect on" \
    -ex "set logging enabled on" \
    -ex "info program" \
    -ex 'p $_siginfo._sifields._sigfault.si_addr' \
    -ex "x/i \$pc" \
    -ex "info registers" \
    -ex "source $root/scripts/gdb_guest.py" \
    -ex 'echo \nGuest registers (if rbx holds the PPCContext):\n' \
    -ex "guest-ctx" \
    -ex 'echo \nGuest object in r3:\n' \
    -ex "guest-obj ctx:r3" \
    -ex 'echo \nGuest object in r31:\n' \
    -ex "guest-obj ctx:r31" \
    -ex 'echo \nGuest stack:\n' \
    -ex "guest-stack" \
    -ex "bt 40" \
    -ex "info threads" \
    -ex "thread apply all bt 40" \
    -ex "set logging enabled off" \
    -ex "kill" \
    --args "$bin" \
        --game_data_root="$root/game/extracted" \
        --log_file="$root/notes/logs/run.log" \
        --log_level=debug \
        --veronicax_show_launcher=false \
        "$@"

[ -f "$out" ] && echo "Thread stacks saved to notes/logs/hang-backtrace.txt"
