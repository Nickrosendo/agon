#!/bin/bash
# The seasons verdict: runs seeds 1 to 15 side by side on the default world and
# applies the pass rule agreed before running (the seasons design, section 8).
# Two parts changed on 2026-11-08, Nicolas's word, after both proved out of
# reach for any world that does not decline: crowding is judged on the whole
# run only (the first ten years are still printed), and an adult's hunger death
# after it reached its ageing span counts as old age (balance_report's
# adult_worn), since ageing ends a life by making it too dear to feed. On
# 2026-11-09, also Nicolas's word, long lives are judged on length only: with
# worlds held at the plants' capacity something must end the lives births add,
# and every option measured left hunger the commonest adult death (51-66 %).
# The causes are still printed.
#
# Usage: bash tests/seasons_verdict.sh [BASELINE] [TICKS] [CONFIG]
#   BASELINE  a file this script printed before, on the code to compare with.
#             With none (or "-"), only the table is printed: that is how a
#             baseline is made.
#   TICKS     ticks per world, 60000 unless given (100 years of 600 ticks).
#   CONFIG    a settings file passed to every world, for trying a change.
#
# Every world's full report is kept beside the table, in the same output, so a
# baseline file also holds the numbers that are reported and never judged.
set -eu

BASELINE=${1:--}
TICKS=${2:-60000}
CONFIG=${3:-}
SEEDS=15
TOOL=build/release/balance_report

make -s balance >/dev/null
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

EXTRA=""
if [ -n "$CONFIG" ]; then
    EXTRA="--config $CONFIG"
fi

# One process per seed, all at once: each world runs on its own core. xargs
# waits for all of them; each writes its own file, so outputs never interleave.
seq 1 "$SEEDS" | xargs -P "$SEEDS" -I{} sh -c \
    "$TOOL --seed {} --ticks $TICKS --summary $EXTRA > $TMP/{}.txt"

echo "seasons verdict: $SEEDS seeds, $TICKS ticks each${CONFIG:+, config $CONFIG}"
echo
echo "SUMMARY seed alive mean10y meanall fruit leaf eaters killers adult_life" \
     "hunger_adult top_adult adult_deaths adult_wounds adult_hunger adult_age" \
     "adult_worn"
for seed in $(seq 1 "$SEEDS"); do
    grep '^SUMMARY' "$TMP/$seed.txt"
done
echo
echo "Reported, never judged:"
for seed in $(seq 1 "$SEEDS"); do
    echo "  seed $seed"
    grep -E '^     (deaths|giants starved|births per season|population as|sleep:|shade:)' \
        "$TMP/$seed.txt"
done

if [ "$BASELINE" = "-" ]; then
    exit 0
fi

echo
echo "Pass rule (the seasons design, section 8), against $BASELINE:"
# awk reads the baseline's SUMMARY lines first (NR == FNR is true only while
# reading the first file), then this run's, and judges at the end.
for seed in $(seq 1 "$SEEDS"); do grep '^SUMMARY' "$TMP/$seed.txt"; done |
awk '
NR == FNR {
    if ($1 == "SUMMARY") { base10[$2] = $4; baseall[$2] = $5 }
    next
}
$1 == "SUMMARY" {
    seed = $2
    seeds++
    if ($3 != 1) next
    alive++
    if (!(seed in base10)) { missing = missing " " seed; next }
    if ($5 > baseall[seed] / 2)
        crowded = crowded sprintf(" %d (%.0f/%.0f, %.0f/%.0f)", seed, $4,
                                  base10[seed], $5, baseall[seed])
    if ($6 <= 0 || $7 <= 0 || $8 <= 0 || $9 <= 0)
        broken = broken sprintf(" %d (fruit %d leaf %d eaters %d killers %d)",
                                seed, $6, $7, $8, $9)
    life += $10 * $13
    adults += $13
    # $17, adult_worn, is part of $15; it moves from hunger to age.
    worn = $17 == "" ? 0 : $17
    wounds += $14; hunger += $15 - worn; age += $16 + worn
}
END {
    fails = 0
    ok = alive >= 12
    printf "  1. most worlds survive: %d of %d alive, 12 needed: %s\n",
           alive, seeds, ok ? "PASS" : "FAIL"
    fails += !ok
    ok = crowded == "" && missing == ""
    printf "  2. half as crowded over the whole run: %s%s%s\n", ok ? "PASS" : "FAIL",
           crowded == "" ? "" : ", too crowded (mean10y/base, meanall/base):" crowded,
           missing == "" ? "" : ", no baseline for:" missing
    fails += !ok
    ok = broken == ""
    printf "  3. still an ecosystem: %s%s\n", ok ? "PASS" : "FAIL",
           broken == "" ? "" : ", missing a part:" broken
    fails += !ok
    mean = adults > 0 ? life / adults : 0
    top = "wounds"; topn = wounds
    if (hunger > topn) { top = "hunger"; topn = hunger }
    if (age > topn) { top = "age"; topn = age }
    ok = mean >= 2400 && adults > 0
    printf "  4. long lives: adults live %.0f ticks on average over %d deaths, 2400 " \
           "needed; adult deaths wounds %d hunger %d age (worn out included) %d, " \
           "commonest %s (reported, not judged): %s\n",
           mean, adults, wounds, hunger, age, top, ok ? "PASS" : "FAIL"
    fails += !ok
    printf "VERDICT %s (%d of 4 conditions failed)\n", fails ? "FAIL" : "PASS", fails
}' "$BASELINE" -
