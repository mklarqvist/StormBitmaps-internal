#!/usr/bin/env bash
# Tile width sweep, ACROSS corpora, against fixed Roaring.
#
# Tile hoisting trades decision quality for decision cost, and the width is the
# knob. census1881 is where the trade shows: its per-pair oracle reads 41.6
# ns/pair, its 64-wide tile policy 52 -- so the decisions are available and the
# granularity is throwing them away. The question is whether a narrower tile
# recovers that WITHOUT costing the corpora where 64 is already right, since
# selection is only 0.12% of runtime at 64 and every halving multiplies the
# decision count by four.
#
# Reported as x-vs-Roaring per width so one width can be chosen on the whole
# corpus set rather than on the corpus that motivated the question.
#
#   bench/tile_size.sh [widths...]        # default 16 32 64 128
set -uo pipefail
cd "$(dirname "$0")/.."
BIN=${BIN:-build_portable/bench_allpairs_fix}
BUDGET=${BUDGET:-1600000000}
SP=${SCRATCH:-/private/tmp/claude-501/-Volumes-SabrentM2-ML-projects-science-StormBitmaps/4d0fb93a-5e8d-46c3-aa16-fe051b5e5b1f/scratchpad}
WIDTHS=${*:-16 32 64 128}

read -r -d '' C <<'EOD' || true
census1881 200
census1881_srt 200
census-income 200
weather_sept_85 200
wikileaks-noquotes 200
dimension_003 15482
dimension_008 5240
dimension_033 173
as-skitter 1478016
wiki-Talk 67492
com-LiveJournal 3212032
soc-Pokec 1277002
gnomad_chr21_exomes_af1e-3 625656
usher_sarscov2 29411
msprime_100k 51062
msprime_10k 41991
EOD
fb(){ for d in data/corpora "$SP/stormbin"; do [ -f "$d/$1.bin" ] && { echo "$d/$1.bin"; return 0; }; done; return 1; }

printf "%-30s" corpus; for w in $WIDTHS; do printf " %9s" "T=$w"; done; echo
while read -r n total; do
  [ -z "$n" ] && continue
  b=$(fb "$n") || continue
  u=$(.venv/bin/python -c "
import struct;f=open('$b','rb');f.read(8);print(struct.unpack('<IIII',f.read(16))[2])")
  r=$(( BUDGET / (u/8 + 1) )); [ $r -gt 512 ] && r=512; [ $r -lt 64 ] && r=64
  s=$(( total / r )); [ $s -lt 1 ] && s=1
  printf "%-30s" "$n"
  for w in $WIDTHS; do
    "$BIN" --file "$b" --rows $r --in-stride $s --tile "$w" 2>/dev/null \
    | awk '/roaring-FIX/{ro=$2}/^per-tile/{pt=$2}END{if(pt>0)printf " %8.2fx",ro/pt;else printf " %9s","-"}'
  done
  echo
done <<< "$C"
