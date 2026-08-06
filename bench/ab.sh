#!/usr/bin/env bash
# PAIRED A/B on one corpus: alternate the two configurations in time.
#
# Why this exists. bench_allpairs controls within-process noise (warm-up pass,
# repeat to a 100 ms floor, minimum taken) and vs_roaring.sh controls
# within-sweep noise (median of three whole-process runs). Neither controls
# BETWEEN-SWEEP state, and that is the one that has been misleading us: the
# point-dispatch commit credited a change with wiki-Talk 2.29x -> 2.77x and
# gnomad 2.74x -> 3.09x by comparing two sweeps run at different times, and a
# within-run A/B against the same code with the feature disabled reproduced
# none of it. enwiki-categorylinks alone has been seen spanning 2.38x to 4.60x
# inside a single table.
#
# The fix is pairing. Run A, then B, then A, then B, so each A is adjacent in
# time to its own B and any drift affects both. Report the median of the
# per-round ratios, not the ratio of the medians -- the paired statistic is
# what survives drift; the unpaired one does not.
#
#   bench/ab.sh <corpus> <rows> <stride> "<A env>" "<B env>" [rounds]
#   bench/ab.sh census1881 373 1 "STORM_POINT_MAX=0" "STORM_POINT_MAX=4" 7
#
# An empty env string means "default configuration".
set -uo pipefail
cd "$(dirname "$0")/.."
BIN=${BIN:-build_portable/bench_allpairs_fix}
SP=${SCRATCH:-/private/tmp/claude-501/-Volumes-SabrentM2-ML-projects-science-StormBitmaps/4d0fb93a-5e8d-46c3-aa16-fe051b5e5b1f/scratchpad}
n=$1; rows=$2; stride=$3; AENV=${4:-}; BENV=${5:-}; ROUNDS=${6:-7}
b=""; for d in data/corpora "$SP/stormbin"; do [ -f "$d/$n.bin" ] && b="$d/$n.bin"; done
[ -n "$b" ] || { echo "corpus $n not found"; exit 1; }

one() {  # $1 = env string; prints per-tile ns/pair
  env $1 "$BIN" --file "$b" --rows "$rows" --in-stride "$stride" --tile 64 2>/dev/null \
  | awk '/^per-tile/{print $2; exit}'
}

rat=""
for r in $(seq "$ROUNDS"); do
  a=$(one "$AENV"); s=$(one "$BENV")
  [ -n "$a" ] && [ -n "$s" ] && rat="$rat$(awk -v x="$a" -v y="$s" 'BEGIN{printf "%.5f\n",x/y}')
"
done
printf '%s' "$rat" | .venv/bin/python -c "
import sys, statistics as st
v=[float(x) for x in sys.stdin.read().split() if x]
if len(v)<3: print('  too few rounds'); raise SystemExit
v.sort()
med=st.median(v)
wins=sum(1 for x in v if x<1.0)
# A/B on ns/pair: <1 means A is FASTER. Report as a speedup of B over A.
print('  A=%-28s B=%-28s' % ('${AENV:-default}','${BENV:-default}'))
print('  paired median A/B = %.3f   (%s)   A faster in %d/%d rounds   spread %.3f-%.3f'
      % (med, 'A faster' if med<1 else ('B faster' if med>1 else 'tie'), wins, len(v), v[0], v[-1]))
"
