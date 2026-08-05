#!/usr/bin/env bash
# Thresholded (opt-in) mode across EVERY corpus at three cutoffs.
#
# The earlier sweep covered only the original 13 corpora and one threshold in
# the README. Row count is budgeted per corpus as elsewhere: at universe 1.3e8 a
# bitmap row is 15.8 MB, so a fixed row count would measure memory pressure
# rather than the algorithm.
set -uo pipefail
cd "$(dirname "$0")/.."
OUT=${1:-results/threshold/all_corpora.csv}
BUDGET=${BUDGET:-1200000000}
SP=${SCRATCH:-/private/tmp/claude-501/-Volumes-SabrentM2-ML-projects-science-StormBitmaps/4d0fb93a-5e8d-46c3-aa16-fe051b5e5b1f/scratchpad}
mkdir -p "$(dirname "$OUT")"
read -r -d '' C <<'EOD' || true
enwiki-categorylinks 2165205 IR
uscensus2000 200 census
dbpedia-link 4384102 graph
wikipedia_link_en 5978043 graph
livejournal-groupmemberships 2197915 graph
com-Orkut 3004674 graph
com-LiveJournal 3212032 graph
soc-Pokec 1277002 graph
as-skitter 1478016 graph
wiki-Talk 67492 graph
dimension_003 15482 druid
dimension_008 5240 druid
dimension_033 173 druid
census1881 200 census
census1881_srt 200 census
wikileaks-noquotes 200 text
weather_sept_85 200 sensor
census-income 200 census
usher_sarscov2 29411 genomics
gnomad_chr21_exomes_af1e-3 625656 genomics
msprime_1M 15592 genomics-sim
msprime_100k 51062 genomics-sim
msprime_10k 41991 genomics-sim
EOD
fb(){ for d in data/corpora "$SP/stormbin"; do [ -f "$d/$1.bin" ] && { echo "$d/$1.bin"; return 0; }; done; return 1; }
echo "corpus,domain,t,N,pairs,hits,exact_ns,prefix_ns,gated_ns,gated_speedup,gate,correct" > "$OUT"
printf "%-30s %-12s %9s %9s %9s\n" corpus domain "t=0.001" "t=0.01" "t=0.1"
while read -r n tot dom; do
  [ -z "$n" ] && continue
  b=$(fb "$n") || continue
  u=$(.venv/bin/python -c "
import struct;f=open('$b','rb');f.read(8);print(struct.unpack('<IIII',f.read(16))[2])")
  r=$(( BUDGET / (u/8 + 1) )); [ $r -gt 384 ] && r=384; [ $r -lt 48 ] && r=48
  s=$(( tot / r )); [ $s -lt 1 ] && s=1
  line=$(printf "%-30s %-12s" "$n" "$dom")
  for t in 0.001 0.01 0.1; do
    v=$(./build_portable/bench_threshold --file "$b" --rows $r --stride $s --t $t \
          --repeats 3 --csv --tag "$n" 2>/dev/null)
    if [ -z "$v" ]; then line="$line $(printf '%9s' 'fail')"; continue; fi
    echo "$v" | .venv/bin/python -c "
import sys
p=sys.stdin.read().strip().split(',')
open('$OUT','a').write('%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n'%(p[0],'$dom',p[1],p[2],p[3],p[4],p[6],p[7],p[8],p[10],p[11],p[12]))
print('%9s'%((p[10][:6]+'x') if p[12]=='1' else 'WRONG'))" > /tmp/_v
    line="$line $(cat /tmp/_v)"
  done
  echo "$line"
done <<< "$C"
