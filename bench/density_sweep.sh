#!/usr/bin/env bash
# Sweep the whole sparsity -> density axis and record every cell at every point.
#
# This is the experiment that tells the story the point measurements only hint
# at: each pairing has a density band where it wins, and the interesting content
# is WHERE THE CURVES CROSS. A single density -- any single density -- makes some
# cell look best and hides the fact that it is best only there.
#
# Both sides of every pair carry the SAME density here (uniform spectrum), so the
# x axis means exactly one thing. The skewed-spectrum corpora in sweep.sh cover
# the asymmetric case; conflating the two would make the curves unreadable.
#
# The universe is fixed at 65,536 bits so cache residency does not drift along
# the x axis: every point has a 1 MB bitmap corpus, L2-resident on this host.
# Without that pin, the low-density end would be measuring L1 and the high end
# DRAM, and the crossings would be artifacts of the memory hierarchy rather than
# of the algorithms (standing rule 5).
#
# Extremes are included deliberately -- 1 bit set, and all-but-one -- because
# that is where the asymptotics are visible and where a Theta(m) kernel is most
# obviously the wrong choice.
set -euo pipefail

BIN=${BIN:-./build/bench_cells}
OUT=${OUT:-results/density.jsonl}
U=${U:-65536}
ROWS=${ROWS:-96}
PAIRS=${PAIRS:-1200}
REPEATS=${REPEATS:-5}

: > "$OUT"

# 1 bit, then log-spaced through the middle, then the dense extremes and the
# mirror-image sparse ones (all-but-one, all).
DENSITIES=$(python3 - "$U" <<'PY'
import sys
u = int(sys.argv[1])
pts  = [1.0/u, 2.0/u, 8.0/u, 32.0/u, 128.0/u]
pts += [1e-3, 3e-3, 0.01, 0.03, 0.05, 0.1, 0.2, 0.3, 0.5, 0.7, 0.9, 0.99]
pts += [1.0 - 32.0/u, 1.0 - 1.0/u, 1.0]
print(' '.join(f'{p:.10g}' for p in sorted(set(pts))))
PY
)

N=$(echo "$DENSITIES" | wc -w | tr -d ' ')
echo "density sweep: $N points, universe=$U rows=$ROWS pairs=$PAIRS repeats=$REPEATS"
i=0
for D in $DENSITIES; do
  i=$((i + 1))
  printf "[%2d/%2d] density=%-12s " "$i" "$N" "$D"
  "$BIN" --cells all --universe "$U" --density "$D" --structure uniform \
         --spectrum uniform --rows "$ROWS" --pairs "$PAIRS" --repeats "$REPEATS" \
         --json "$OUT" > /dev/null 2>&1
  printf "ok\n"
done
echo "wrote $OUT ($(wc -l < "$OUT" | tr -d ' ') records)"
