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

# REPS whole-process repeats per corpus, median reported, min-max shown.
#
# bench_allpairs already repeats internally to a 100 ms floor, which removes
# within-process jitter. It cannot remove BETWEEN-process state: this sweep
# walks corpora whose working sets run to 1.6 GB, and on a machine with 68 MB
# free the page cache a corpus inherits from its predecessor is part of its
# measurement. census1881 read 0.98x and 0.65x on consecutive sweeps for that
# reason. A single number would report the second as a loss; the median plus
# the observed range says what actually happened.
REPS=${REPS:-3}

echo "corpus,domain,universe,rows,roaring_ns,allbitmap_ns,pertile_ns,probe_ns,best_ns,vs_roaring,vs_roaring_lo,vs_roaring_hi,cells" > "$OUT"
printf "%-30s %-11s %12s %10s %10s %9s %-15s %s\n" \
       corpus domain universe roaring refine "vs roar" "(range)" "cell mix (refine)"
win=0; tot=0
while read -r n total dom; do
  [ -z "$n" ] && continue
  b=$(fb "$n") || continue
  u=$(.venv/bin/python -c "
import struct;f=open('$b','rb');f.read(8);print(struct.unpack('<IIII',f.read(16))[2])")
  r=$(( BUDGET / (u/8 + 1) )); [ $r -gt 512 ] && r=512; [ $r -lt 64 ] && r=64
  s=$(( total / r )); [ $s -lt 1 ] && s=1
  raw=""
  for k in $(seq "$REPS"); do
    raw="$raw
$("$BIN" --file "$b" --rows $r --in-stride $s --tile 64 $ZMFLAG --tag "$n" 2>/dev/null)"
  done
  line=$(printf '%s' "$raw" | .venv/bin/python -c "
import sys,re,statistics as st
t=sys.stdin.read()
f=lambda p:[float(x) for x in re.findall(p,t,re.M)]
ro=f(r'^roaring(?:-FIX)?\s+([\d.]+)'); ab=f(r'^all-bitmap\s+([\d.]+)')
pt=f(r'^per-tile\s+([\d.]+)');        pb=f(r'^probe\s+([\d.]+)')
rf=f(r'^refine\s+([\d.]+)')
mix=re.findall(r'^refine.*\n.*\n\s+(.*?)\s*\$', t, re.M)
mix=(mix[-1] if mix else '').strip()
if not(ro and ab and pt and pb and rf): print('FAIL|%-30s (parse failed)'%'$n'); sys.exit()
k=min(len(ro),len(pt),len(pb),len(rf))
# ONE policy, the one a caller actually gets -- REFINE, which is per-tile plus
# the heterogeneity-gated per-pair choice between the tile's top two
# candidates. Still a single policy, not a best-of: the gate is computed from
# the data at tile-decision time, not chosen per corpus with hindsight.
# This was min(pt, pb, rf), which picks the best POLICY per corpus with
# hindsight. That is the same defect as the best-fixed-cell table this
# file replaced, and it inflated the win count: refine is a net loss
# across the corpus set (bench/refine_gate.sh) and was still being
# credited on the two corpora where it happens to win.
best=[rf[i] for i in range(k)]
rat=sorted(ro[i]/best[i] for i in range(k))
med=st.median(rat)
print('%s|%-30s %-11s %12s %10.2f %10.2f %8.2fx %-15s %s'%(
      'WIN' if med>=1.0 else 'LOSS','$n','$dom',format(int($u),','),
      st.median(ro),st.median(rf),med,'[%.2f-%.2f]'%(rat[0],rat[-1]),mix))
print('CSV|%s,%s,%s,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%s'%(
      '$n','$dom',$u,'$r',st.median(ro),st.median(ab),st.median(pt),st.median(pb),
      st.median(best),med,rat[0],rat[-1],mix.replace(',',';')))
")
  echo "$line" | grep -v '^CSV|' | sed 's/^WIN|/  /; s/^LOSS|/! /; s/^FAIL|/? /'
  echo "$line" | grep '^CSV|' | sed 's/^CSV|//' >> "$OUT"
  case "$line" in WIN*) win=$((win+1)); tot=$((tot+1));; LOSS*) tot=$((tot+1));; esac
done <<< "$C"
echo
echo "$win/$tot corpora beat fixed Roaring   (median of $REPS runs; lines marked ! are losses)"
echo "csv: $OUT"
