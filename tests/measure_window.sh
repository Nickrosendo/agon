#!/bin/sh
# Measures how heavy agon-window is to leave running, so a later look can be
# held to "not measurably heavier than before".
# Usage: sh tests/measure_window.sh SAVE [SECONDS] [--dummy] [--paused]
#   SAVE      a saved world, loaded by both programs (the same file every time)
#   SECONDS   how long to measure after a 10 second warm-up (default 60)
#   --dummy   SDL's dummy video driver: no display needed, repeatable
#   --paused  start paused: the cost of a window nobody is watching change
# Needs build/release/agon and build/release/agon-window
# (make release && make window MODE=release). Linux only (/proc).
# Prints: cpu 23.4% rss 41230kB agon 4120kB over 37110kB
#   cpu    the window's share of one core over the measured seconds
#   rss    the window's resident memory at the end
#   agon   peak memory of the terminal program on the same world
#   over   rss minus agon: what the window itself adds
SAVE=$1
SECONDS_TO_MEASURE=60
EXTRA=
shift
for arg in "$@"; do
    case $arg in
    --dummy) export SDL_VIDEODRIVER=dummy ;;
    --paused) EXTRA="--paused" ;;
    *[!0-9]* | '') echo "measure_window: unknown argument $arg" >&2; exit 2 ;;
    *) SECONDS_TO_MEASURE=$arg ;;
    esac
done
[ -f "$SAVE" ] || { echo "usage: measure_window.sh SAVE [SECONDS] [--dummy] [--paused]" >&2; exit 2; }
WINDOW=build/release/agon-window
TERMINAL=build/release/agon
[ -x $WINDOW ] && [ -x $TERMINAL ] || { echo "measure_window: build the release programs first" >&2; exit 2; }

TICKS=$(getconf CLK_TCK)

# $EXTRA is left unquoted on purpose: empty, it vanishes.
$WINDOW --load "$SAVE" --speed 10 --zoom normal $EXTRA >/dev/null 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null' EXIT

# utime + stime: fields 14 and 15 of /proc/PID/stat. The program's name is
# field 2, in brackets, so cut everything up to the last ')' first.
cpu_ticks() {
    sed 's/^.*) //' /proc/$PID/stat | awk '{print $12 + $13}'
}
rss_kb() {
    awk '/^VmRSS:/ {print $2}' /proc/$PID/status
}

sleep 10
kill -0 $PID 2>/dev/null || { echo "measure_window: the window exited early" >&2; exit 1; }
START=$(cpu_ticks)
sleep "$SECONDS_TO_MEASURE"
kill -0 $PID 2>/dev/null || { echo "measure_window: the window exited early" >&2; exit 1; }
END=$(cpu_ticks)
RSS=$(rss_kb)
kill $PID 2>/dev/null
wait $PID 2>/dev/null

# Peak memory in kB. GNU time (/usr/bin/time) when installed; otherwise python's
# getrusage, which reports the same number (ru_maxrss, in kB on Linux).
if [ -x /usr/bin/time ]; then
    AGON=$(/usr/bin/time -f %M $TERMINAL --load "$SAVE" --once --ticks 600 2>&1 >/dev/null | tail -1)
else
    AGON=$(python3 -I -c 'import resource, subprocess, sys
subprocess.run(sys.argv[1:], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
print(resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss)' \
        $TERMINAL --load "$SAVE" --once --ticks 600)
fi

awk -v s="$START" -v e="$END" -v t="$TICKS" -v secs="$SECONDS_TO_MEASURE" \
    -v rss="$RSS" -v agon="$AGON" 'BEGIN {
    printf "cpu %.1f%% rss %dkB agon %dkB over %dkB\n",
           (e - s) / t / secs * 100, rss, agon, rss - agon }'
