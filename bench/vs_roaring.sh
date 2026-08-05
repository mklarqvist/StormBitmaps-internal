#!/usr/bin/env bash
# Storm's ONLINE selection against fixed CRoaring, on identical pairs.
#
# This is the head-to-head the project is judged on. Every other table here
# reports Storm against all-bitmap, which answers "does selection help" but not
# "is this better than what people actually use". CRoaring with run_optimize()
# plus the C35 array->bitset promotion is the competently-tuned baseline that
# standing rule 9 requires, and bench_allpairs times it inside the same process
# on the same rows and the same pair set -- so the comparison is one measurement
# rather than two runs stitched together.
#
# Both sides are best-of-3 (bench_allpairs.cpp). They were not always: Roaring
# was repeated and Storm was not, which understated Storm by the cost of a cold
# first pass over a working set far larger than L2.
#
#   bench/vs_roaring.sh [outfile]        # default results/vs_roaring.csv
#   ZMFLAG=--no-zonemap bench/vs_roaring.sh results/vs_roaring_nozm.csv
set -uo pipefail
cd "$(dirname "$0")/.."
OUT=${1:-results/vs_roaring.csv}
ZMFLAG=${ZMFLAG:-}
BUDGET=${BUDGET:-1600000000}
BIN=${BIN:-build_portable/bench_allpairs_fix}
SP=${SCRATCH:-/private/tmp/claude-501/-Volumes-SabrentM2-ML-projects-science-StormBitmaps/4d0fb93a-5e8d-46c3-aa16-fe051b5e5b1f/scratchpad}
mkdir -p "$(dirname "$OUT")"
[ -x "$BIN" ] || { echo "build $BIN first"; exit 1; }

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

echo "corpus,domain,universe,rows,roaring_ns,allbitmap_ns,pertile_ns,probe_ns,best_ns,vs_roaring,cells" > "$OUT"
printf "%-30s %-11s %12s %10s %10s %10s %9s  %s\n" \
       corpus domain universe roaring per-tile probe "vs roar" "cell mix (per-tile)"
win=0; tot=0
while read -r n total dom; do
  [ -z "$n" ] && continue
  b=$(fb "$n") || continue
  u=$(.venv/bin/python -c "
import struct;f=open('$b','rb');f.read(8);print(struct.unpack('<IIII',f.read(16))[2])")
  r=$(( BUDGET / (u/8 + 1) )); [ $r -gt 512 ] && r=512; [ $r -lt 64 ] && r=64
  s=$(( total / r )); [ $s -lt 1 ] && s=1
  line=$("$BIN" --file "$b" --rows $r --in-stride $s --tile 64 $ZMFLAG --tag "$n" 2>/dev/null \
  | .venv/bin/python -c "
import sys,re
t=sys.stdin.read()
g=lambda p:(lambda m:m.group(1) if m else None)(re.search(p,t,re.M))
ro=g(r'^roaring(?:-FIX)?\s+([\d.]+)'); ab=g(r'^all-bitmap\s+([\d.]+)')
pt=g(r'^per-tile\s+([\d.]+)');        pb=g(r'^probe\s+([\d.]+)')
# the cell-mix line printed under per-tile
mix=re.search(r'^per-tile.*\n.*\n\s+(.*?)\s*$', t, re.M)
mix=(mix.group(1) if mix else '').strip()
if not(ro and ab and pt and pb): print('FAIL|%-30s (parse failed)'%'$n'); sys.exit()
ro,ab,pt,pb=float(ro),float(ab),float(pt),float(pb)
best=min(pt,pb)
print('%s|%-30s %-11s %12s %10.2f %10.2f %10.2f %8.2fx  %s'%(
      'WIN' if best<ro else 'LOSS','$n','$dom',format(int($u),','),ro,pt,pb,ro/best,mix))
print('CSV|%s,%s,%s,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,%s'%(
      '$n','$dom',$u,'$r',ro,ab,pt,pb,best,ro/best,mix.replace(',',';')))
")
  echo "$line" | grep -v '^CSV|' | sed 's/^WIN|/  /; s/^LOSS|/! /; s/^FAIL|/? /'
  echo "$line" | grep '^CSV|' | sed 's/^CSV|//' >> "$OUT"
  case "$line" in WIN*) win=$((win+1)); tot=$((tot+1));; LOSS*) tot=$((tot+1));; esac
done <<< "$C"
echo
echo "$win/$tot corpora beat fixed Roaring   (lines marked ! are losses)"
echo "csv: $OUT"
