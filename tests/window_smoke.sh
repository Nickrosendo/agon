#!/bin/sh
# Runs the window program on SDL's dummy video driver, which needs no screen.
# Usage: tests/window_smoke.sh build/test
OUT=$1
export SDL_VIDEODRIVER=dummy
# SDL's drivers keep some memory on purpose, for the life of the program; the
# leak checker would report that as ours. Bad accesses are still caught.
export ASAN_OPTIONS=detect_leaks=0
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
failed=0

fail() {
    echo "window_smoke: FAILED: $1" >&2
    failed=1
}

# Windows has no terminal program, so the checks against it have a stand-in.
if [ -x "$OUT/agon" ]; then
    HAVE_AGON=1
else
    HAVE_AGON=0
fi

# 1. The window's own path gives the same world as a plain run. Without one to
# compare with: the same world as a second run of itself.
window=$("$OUT/agon-window" --seed 3 --size 64x32 --speed 30 --frames 500 --checksum)
if [ $HAVE_AGON -eq 1 ]; then
    plain=$("$OUT/agon" --once --seed 3 --size 64x32 --ticks 500 --checksum | grep '^checksum')
else
    plain=$("$OUT/agon-window" --seed 3 --size 64x32 --speed 30 --frames 500 --checksum)
fi
[ -n "$window" ] && [ "$window" = "$plain" ] ||
    fail "checksums differ: window '$window', plain '$plain'"

# 2. Quitting saves, by the same path as the window's X button. Twice, because
# replacing an existing file is what Windows does differently.
for attempt in 1 2; do
    "$OUT/agon-window" --seed 3 --size 64x32 --frames 10 --save "$TMP/w.save" ||
        fail "the run with --save failed (attempt $attempt)"
done
if [ $HAVE_AGON -eq 1 ]; then
    "$OUT/agon" --load "$TMP/w.save" --once --ticks 0 >/dev/null ||
        fail "the saved world did not load"
else
    "$OUT/agon-window" --load "$TMP/w.save" --frames 1 >/dev/null ||
        fail "the saved world did not load"
fi

# 3. --once is refused, with the command-line mistake exit code.
"$OUT/agon-window" --once >/dev/null 2>&1
[ $? -eq 2 ] || fail "--once was not refused with exit code 2"

# 4. And the terminal program refuses the window's options.
if [ $HAVE_AGON -eq 1 ]; then
    "$OUT/agon" --frames 5 >/dev/null 2>&1
    [ $? -eq 2 ] || fail "agon accepted --frames"
fi

# 5. Every zoom step draws, and writes a picture of the right size.
for zoom in close normal far world; do
    "$OUT/agon-window" --seed 3 --size 64x32 --frames 5 --zoom "$zoom" \
        --screenshot "$TMP/$zoom.ppm" || fail "the run at $zoom failed"
    [ "$(head -c 16 "$TMP/$zoom.ppm" | tr '\n' ' ')" = "P6 1280 720 255 " ] ||
        fail "the screenshot at $zoom has the wrong header"
done
"$OUT/agon-window" --zoom sideways >/dev/null 2>&1
[ $? -eq 2 ] || fail "a bad --zoom was not refused"

# 6. A world that dies. A small world has too few founders to last: all six seeds
# tried at 40x20 were empty by tick 5,000. The world stops on the tick it ended,
# so more frames must not change it, and its save loads to the same dead world.
dead_a=$("$OUT/agon-window" --seed 1 --size 40x20 --speed 120 --frames 2500 \
    --save "$TMP/dead.save" --screenshot "$TMP/dead.ppm" --checksum) ||
    fail "the dying world's run failed"
dead_b=$("$OUT/agon-window" --seed 1 --size 40x20 --speed 120 --frames 3000 --checksum) ||
    fail "the longer dying world's run failed"
[ -n "$dead_a" ] && [ "$dead_a" = "$dead_b" ] ||
    fail "a dead world went on changing: '$dead_a', then '$dead_b'"
dead_c=$("$OUT/agon-window" --load "$TMP/dead.save" --frames 3 --checksum) ||
    fail "the dead save did not load"
[ "$dead_a" = "$dead_c" ] ||
    fail "the dead save came back different: '$dead_a', then '$dead_c'"
if [ $HAVE_AGON -eq 1 ]; then
    text=$("$OUT/agon" --load "$TMP/dead.save" --once --ticks 100 | tail -6)
    [ $? -eq 0 ] || fail "--once on a dead world did not exit 0"
    case "$text" in
    *"game over"*"last creature died"*"most alive"*"oldest"*"largest"*) ;;
    *) fail "--once did not print the game over lines: $text" ;;
    esac
fi

# 7. The life story page and jumping to a giant are only looking: pressing l, j,
# Esc and l again leaves the world exactly as a run without keys. Each run's
# keys come in its last frames, so the screenshot shows the page open.
keyed=$("$OUT/agon-window" --seed 3 --size 64x32 --speed 30 --frames 500 \
    --keys "ljlj" --screenshot "$TMP/page.ppm" --checksum) ||
    fail "the run with --keys failed"
[ -n "$keyed" ] && [ "$keyed" = "$window" ] ||
    fail "pressing l and j changed the world: '$keyed' against '$window'"
[ "$(head -c 16 "$TMP/page.ppm" | tr '\n' ' ')" = "P6 1280 720 255 " ] ||
    fail "the screenshot with the page open has the wrong header"
if [ $HAVE_AGON -eq 1 ]; then
    "$OUT/agon" --keys l >/dev/null 2>&1
    [ $? -eq 2 ] || fail "agon accepted --keys"
fi

# 8. The species lab is only looking: pressing b leaves the world exactly as a run
# without keys, and the picture is the right size.
lab=$("$OUT/agon-window" --seed 3 --size 64x32 --speed 30 --frames 500 \
    --keys b --screenshot "$TMP/lab.ppm" --checksum) ||
    fail "the run with the lab open failed"
[ -n "$lab" ] && [ "$lab" = "$window" ] ||
    fail "opening the lab changed the world: '$lab' against '$window'"
[ "$(head -c 16 "$TMP/lab.ppm" | tr '\n' ' ')" = "P6 1280 720 255 " ] ||
    fail "the screenshot with the lab open has the wrong header"

# 9. --paused only looks: the world stays at tick 0, and the terminal program
# refuses the option.
paused=$("$OUT/agon-window" --seed 3 --size 64x32 --frames 50 --paused --checksum) ||
    fail "the run with --paused failed"
if [ $HAVE_AGON -eq 1 ]; then
    zero=$("$OUT/agon" --once --seed 3 --size 64x32 --ticks 0 --checksum | grep '^checksum')
    "$OUT/agon" --paused >/dev/null 2>&1
    [ $? -eq 2 ] || fail "agon accepted --paused"
    "$OUT/agon" --no-art >/dev/null 2>&1
    [ $? -eq 2 ] || fail "agon accepted --no-art"
else
    zero=$("$OUT/agon-window" --seed 3 --size 64x32 --frames 1 --paused --checksum)
fi
[ -n "$paused" ] && [ "$paused" = "$zero" ] ||
    fail "--paused did not stay at tick 0: '$paused' against '$zero'"

# 10. A world code opens the same world: c prints the code after a year of
# ancestors (paused, so the world stays at that tick), and --code, given the
# code alone or inside a link to the browser page, replays it to the same
# fingerprint. A text that is not a code, and --code with --load, are refused.
copied=$("$OUT/agon-window" --seed 5 --size 64x32 --ancestors 1 --paused \
    --frames 3 --keys c --checksum) || fail "the run that copies a code failed"
code=$(echo "$copied" | sed -n 's/^world code: //p')
want=$(echo "$copied" | grep '^checksum')
[ -n "$code" ] || fail "c printed no world code"
for given in "$code" "https://example.org/agon/?seed=9&code=$code#top"; do
    got=$("$OUT/agon-window" --code "$given" --paused --frames 3 --checksum)
    [ -n "$want" ] && [ "$got" = "$want" ] ||
        fail "--code opened another world: '$got' against '$want'"
done
"$OUT/agon-window" --code hello --frames 1 >/dev/null 2>&1
[ $? -eq 2 ] || fail "a --code that is not a code was not refused"
"$OUT/agon-window" --code "$code" --load "$TMP/w.save" --frames 1 >/dev/null 2>&1
[ $? -eq 2 ] || fail "--code with --load was not refused"

# 11. --playlog writes the sitting's record on leaving: keys counted, the
# Sandbox reached; and the terminal program refuses it.
"$OUT/agon-window" --seed 3 --size 64x32 --frames 20 --keys "mgg" \
    --playlog "$TMP/play.txt" >/dev/null || fail "the run with --playlog failed"
grep -q '^key g 2$' "$TMP/play.txt" && grep -q '^key m 1$' "$TMP/play.txt" &&
    ! grep -q '^first_sandbox_second -1$' "$TMP/play.txt" ||
    fail "the playlog is not as expected: $(tr '\n' '|' < "$TMP/play.txt")"
if [ $HAVE_AGON -eq 1 ]; then
    "$OUT/agon" --playlog "$TMP/x.txt" >/dev/null 2>&1
    [ $? -eq 2 ] || fail "agon accepted --playlog"
fi

[ $failed -eq 0 ] && echo "window_smoke: all passed"
exit $failed
