#!/bin/bash
# The tactics verdict: runs seeds 1 to 15 side by side and applies rules 1 to 3
# of the pass rule agreed before running (the tactics design, section 11):
#   1. every seed shows at least one tactic in the watched years;
#   2. at least 4 of the 7 tactics appear across the seeds, and no one set of
#      tactics is shared by more than 7 seeds;
#   3. a hunter tactic (night, pack, strays) in at least 8 seeds, and a
#      plant-eater tactic (defence, shield, huddle) in at least 8. Alarm calls
#      count for neither side: they can belong to either.
# Rules 4 (blinded reruns) and 5 (the seasons verdict) are run by hand.
#
# Usage: bash tests/tactics_verdict.sh [FROM] [YEARS] [CONFIG]
#   FROM    the tick the watched years start at: the end of the prelude. 0 until
#           worlds start from evolved ancestors.
#   YEARS   watched years, 3 unless given.
#   CONFIG  a settings file passed to every world, for trying a change.
#
# Each seed's TACTIC lines (highest lift, families showing it, families of its
# diet) are printed below the table: how varied a world is inside is printed,
# never judged.
set -eu

FROM=${1:-0}
YEARS=${2:-3}
CONFIG=${3:-}
SEEDS=15
YEAR_TICKS=600
TICKS=$((FROM + YEARS * YEAR_TICKS))
TOOL=build/release/balance_report

make -s balance >/dev/null
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

EXTRA=""
if [ -n "$CONFIG" ]; then
    EXTRA="--config $CONFIG"
fi

seq 1 "$SEEDS" | xargs -P "$SEEDS" -I{} sh -c \
    "$TOOL --seed {} --ticks $TICKS --tactics $FROM $EXTRA > $TMP/{}.txt"

echo "tactics verdict: $SEEDS seeds, watched from tick $FROM for $YEARS years${CONFIG:+, config $CONFIG}"
echo
echo "TACTICS seed count tactics"
for seed in $(seq 1 "$SEEDS"); do
    grep '^TACTICS ' "$TMP/$seed.txt"
done
echo
for seed in $(seq 1 "$SEEDS"); do
    grep '^TACTIC ' "$TMP/$seed.txt" || true
done
echo

# awk reads every seed's TACTICS line and judges the three rules.
for seed in $(seq 1 "$SEEDS"); do
    grep '^TACTICS ' "$TMP/$seed.txt"
done | awk -v seeds="$SEEDS" '
{
    n = $3; list = $4
    if (n > 0) with_any++
    sets[list]++
    if (list ~ /night|pack|strays/) hunter++
    if (list ~ /defence|shield|huddle/) plant++
    split(list, names, ",")
    for (i in names) if (names[i] != "-") kinds[names[i]] = 1
}
END {
    nkinds = 0; for (k in kinds) nkinds++
    top = 0; for (s in sets) if (sets[s] > top && s != "-") top = sets[s]
    r1 = (with_any == seeds) ? "PASS" : "FAIL"
    r2 = (nkinds >= 4 && top <= 7) ? "PASS" : "FAIL"
    r3 = (hunter >= 8 && plant >= 8) ? "PASS" : "FAIL"
    printf "rule 1, a tactic in every seed: %d of %d -> %s\n", with_any, seeds, r1
    printf "rule 2, variety: %d kinds, largest shared set %d seeds -> %s\n", nkinds, top, r2
    printf "rule 3, both sides: hunter tactics in %d, plant-eater tactics in %d -> %s\n", hunter, plant, r3
    passed = (r1 == "PASS") + (r2 == "PASS") + (r3 == "PASS")
    printf "verdict: %d of 3\n", passed
}'
