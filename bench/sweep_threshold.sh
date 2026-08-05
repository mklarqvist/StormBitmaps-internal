#!/usr/bin/env bash
# Sweep the similarity threshold across every corpus (RESEARCH_PLAN.md §15.23).
#
# Thresholded mode is an OPT-IN contract: exact all-pairs stays the default, and
# the caller supplies t only when the application permits it. The speedup is
# therefore a function of a parameter the *user* sets, not a single headline
# number -- which is exactly why it needs a curve rather than a table. This
# sweeps t linearly so the curve can be read at whatever cutoff an application
# actually uses.
#
#   bench/sweep_threshold.sh [outfile]      # default results/threshold/sweep.csv
#
# Env: STEPS (default 50), TMIN (0.001), TMAX (0.5), REPEATS (3)
set -euo pipefail
cd "$(dirname "$0")/.."

OUT=${1:-results/threshold/sweep.csv}
SP=${SCRATCH:-/private/tmp/claude-501/-Volumes-SabrentM2-ML-projects-science-StormBitmaps/4d0fb93a-5e8d-46c3-aa16-fe051b5e5b1f/scratchpad}
STEPS=${STEPS:-50}; TMIN=${TMIN:-0.001}; TMAX=${TMAX:-0.5}; REPEATS=${REPEATS:-3}
BIN=build_portable/bench_threshold
mkdir -p "$(dirname "$OUT")"

[ -x "$BIN" ] || { echo "build $BIN first"; exit 1; }

# corpus rows stride -- row counts chosen so each corpus contributes a
# comparable pair count; stride spans the corpus rather than truncating it.
read -r -d '' CORPORA <<'EOF' || true
as-skitter 256 5773
com-LiveJournal 256 12547
soc-Pokec 256 4988
com-Orkut 256 11737
wiki-Talk 256 263
dimension_003 256 60
dimension_008 256 20
uscensus2000 200 1
census1881 200 1
census1881_srt 200 1
wikileaks-noquotes 200 1
weather_sept_85 200 1
census-income 200 1
EOF

echo "corpus,t,N,pairs,hits,candidates,exact_ns,prefix_ns,gated_ns,speedup,gated_speedup,gate,correct" > "$OUT"

ts=$(.venv/bin/python -c "
lo,hi,n=$TMIN,$TMAX,$STEPS
print(' '.join('%.6f'%(lo+(hi-lo)*i/(n-1)) for i in range(n)))")

nc=$(echo "$CORPORA" | grep -c . || true)
echo "sweeping $STEPS thresholds x $nc corpora -> $OUT"

while read -r name rows stride; do
  [ -z "$name" ] && continue
  [ -f "$SP/stormbin/$name.bin" ] || { echo "  skip $name (not fetched)"; continue; }
  printf '  %-24s' "$name"
  for t in $ts; do
    # A failed run (non-zero exit = hit-set mismatch) must abort the sweep rather
    # than silently drop a row: a missing point would read as a gap in the curve.
    if ! "$BIN" --file "$SP/stormbin/$name.bin" --rows "$rows" --stride "$stride" \
         --t "$t" --repeats "$REPEATS" --csv --tag "$name" >> "$OUT" 2>/dev/null; then
      echo " MISMATCH at t=$t"; exit 1
    fi
  done
  echo " done"
done <<< "$CORPORA"

echo
echo "rows: $(( $(wc -l < "$OUT") - 1 ))"
echo "plot: .venv/bin/python bench/plot_threshold.py $OUT"
