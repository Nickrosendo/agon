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

[ $failed -eq 0 ] && echo "window_smoke: all passed"
exit $failed
