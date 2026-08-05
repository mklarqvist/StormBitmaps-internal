#!/usr/bin/env bash
# STORM_REFINE_RATIO sweep: when should a tile refine per pair?
#
# Policy::Refine evaluates the tile's top two candidates per pair (~2 ns). That
# is free against census1881's ~50 ns/pair and ruinous against dimension_008's
# ~6, where it turned a 1.19x win into 0.75x. And on dimension_008 the per-pair
# decision is not just costlier but WORSE -- the free-decision Oracle measures
# 7.4-9.6 against the tile policy's 6.1, because a tile aggregate smooths noise
# that per-pair metadata still carries.
#
# So refinement is a trade, gated on the tile's top-two predicted-cost RATIO:
# refine only where the runner-up is close enough that a pair could plausibly
# flip. ratio=1 never refines (collapses to PerTile); a large ratio always does.
#
#   bench/refine_gate.sh [ratios...]      # default 1 2 3 5 10 1e9
set -uo pipefail
cd "$(dirname "$0")/.."
BIN=${BIN:-build_portable/bench_allpairs_fix}
BUDGET=${BUDGET:-1600000000}
REPS=${REPS:-3}
SP=${SCRATCH:-/private/tmp/claude-501/-Volumes-SabrentM2-ML-projects-science-StormBitmaps/4d0fb93a-5e8d-46c3-aa16-fe051b5e5b1f/scratchpad}
RATIOS=${*:-1 2 3 5 10 1e9}

read -r -d '' C <<'EOD' || true
census1881 200
census1881_srt 200
dimension_008 5240
dimension_003 15482
dimension_033 173
as-skitter 1478016
wiki-Talk 67492
com-LiveJournal 3212032
soc-Pokec 1277002
weather_sept_85 200
census-income 200
wikileaks-noquotes 200
gnomad_chr21_exomes_af1e-3 625656
usher_sarscov2 29411
msprime_100k 51062
msprime_10k 41991
uscensus2000 200
EOD
fb(){ for d in data/corpora "$SP/stormbin"; do [ -f "$d/$1.bin" ] && { echo "$d/$1.bin"; return 0; }; done; return 1; }

printf "%-30s"; for r in $RATIOS; do printf " %8s" "k=$r"; done; echo
printf "%-30s" corpus; for r in $RATIOS; do printf " %8s" ""; done; echo
while read -r n total; do
  [ -z "$n" ] && continue
  b=$(fb "$n") || continue
  u=$(.venv/bin/python -c "
import struct;f=open('$b','rb');f.read(8);print(struct.unpack('<IIII',f.read(16))[2])")
  rows=$(( BUDGET / (u/8 + 1) )); [ $rows -gt 512 ] && rows=512; [ $rows -lt 64 ] && rows=64
  s=$(( total / rows )); [ $s -lt 1 ] && s=1
  printf "%-30s" "$n"
  for k in $RATIOS; do
    for i in $(seq "$REPS"); do
      STORM_REFINE_RATIO=$k "$BIN" --file "$b" --rows $rows --in-stride $s --tile 64 2>/dev/null \
      | awk '/roaring-FIX/{ro=$2}/^refine/{rf=$2}END{if(rf>0)printf "%.4f\n",ro/rf}'
    done | sort -n | sed -n "$(( (REPS+1)/2 ))p" | awk '{printf " %7.2fx",$1}'
  done
  echo
done <<< "$C"
