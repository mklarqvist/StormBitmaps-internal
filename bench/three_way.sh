#!/usr/bin/env bash
# Storm vs STOCK CRoaring vs FIXED CRoaring, all three on identical pairs.
#
# The distinction between the two Roarings is C35, this project's own finding:
# CRoaring's constructor keeps an array container up to 4096 elements, and on
# these corpora an array of a few hundred 16-bit keys is slower to intersect
# than the 8 kB bitset that replaces it. croaring_modified adds
# roaring_bitmap_storm_promote_arrays(), which promotes arrays above
# STORM_CTOR_BITSET_THRESHOLD (64) to bitsets after run_optimize().
#
# Reporting all three matters because they answer different questions:
#   vs stock  -- what a user of CRoaring today would see
#   vs fixed  -- whether the pairing matrix beats Roaring once Roaring is
#                given the best configuration we know how to give it, which is
#                the comparison standing rule 9 requires
#
# Both Roarings and Storm are timed inside the same process on the same rows,
# same warm-up, same 100 ms floor, min taken. Two binaries are needed because
# the container policy is a compile-time property of CRoaring, so Storm is
# measured twice -- once beside each -- and both figures are printed. They
# should agree; where they do not, the spread is this machine's drift and the
# Roaring comparisons inherit it.
#
#   bench/three_way.sh [outfile]
set -uo pipefail
cd "$(dirname "$0")/.."
OUT=${1:-results/three_way.csv}
REPS=${REPS:-3}
BUDGET=${BUDGET:-1600000000}
SP=${SCRATCH:-/private/tmp/claude-501/-Volumes-SabrentM2-ML-projects-science-StormBitmaps/4d0fb93a-5e8d-46c3-aa16-fe051b5e5b1f/scratchpad}
STOCK=build_portable/bench_allpairs
FIXED=build_portable/bench_allpairs_fix
mkdir -p "$(dirname "$OUT")"
for b in "$STOCK" "$FIXED"; do [ -x "$b" ] || { echo "build $b first"; exit 1; }; done

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

# median of REPS for one (binary, field) pair
med(){ # $1 bin  $2 file  $3 rows  $4 stride  $5 awk-pattern
  local i
  for i in $(seq "$REPS"); do
    "$1" --file "$2" --rows "$3" --in-stride "$4" --tile 64 2>/dev/null \
    | awk -v p="$5" '$0 ~ p {print $2; exit}'
  done | sort -n | sed -n "$(( (REPS+1)/2 ))p"
}

echo "corpus,domain,universe,rows,roaring_stock_ns,roaring_fixed_ns,storm_ns,vs_stock,vs_fixed,fix_gain" > "$OUT"
printf "%-30s %12s %10s %10s %10s %9s %9s %8s\n" \
       corpus universe "stock Ro" "fixed Ro" "Storm" "vs stock" "vs fixed" "fix gain"
ws=0; wf=0; tot=0
while read -r n total dom; do
  [ -z "$n" ] && continue
  b=$(fb "$n") || continue
  u=$(.venv/bin/python -c "
import struct;f=open('$b','rb');f.read(8);print(struct.unpack('<IIII',f.read(16))[2])")
  r=$(( BUDGET / (u/8 + 1) )); [ $r -gt 512 ] && r=512; [ $r -lt 64 ] && r=64
  s=$(( total / r )); [ $s -lt 1 ] && s=1

  ro_s=$(med "$STOCK" "$b" "$r" "$s" '^roaring ')
  st_a=$(med "$STOCK" "$b" "$r" "$s" '^refine ')
  ro_f=$(med "$FIXED" "$b" "$r" "$s" '^roaring-FIX ')
  st_b=$(med "$FIXED" "$b" "$r" "$s" '^refine ')
  [ -n "$ro_s" ] && [ -n "$ro_f" ] && [ -n "$st_a" ] && [ -n "$st_b" ] || {
      printf "%-30s (parse failed)\n" "$n"; continue; }

  read -r line csv <<<"$(.venv/bin/python -c "
ro_s,ro_f,st=$ro_s,$ro_f,($st_a+$st_b)/2.0
vs_s,vs_f,fix=ro_s/st,ro_f/st,ro_s/ro_f
print('%-30s %12s %10.2f %10.2f %10.2f %8.2fx %8.2fx %7.2fx|%s,%s,%s,%s,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f'%(
  '$n',format(int($u),','),ro_s,ro_f,st,vs_s,vs_f,fix,
  '$n','$dom',$u,'$r',ro_s,ro_f,st,vs_s,vs_f,fix))" | tr '|' ' ')"
  echo "$line" | sed "s/^/$( [ "$(echo "$ro_f $st_b" | awk '{print ($1<$2)}')" = 1 ] && echo '! ' || echo '  ')/"
  echo "$csv" >> "$OUT"
  tot=$((tot+1))
  awk -v a="$ro_s" -v b="$st_a" 'BEGIN{exit !(a>b)}' && ws=$((ws+1))
  awk -v a="$ro_f" -v b="$st_b" 'BEGIN{exit !(a>b)}' && wf=$((wf+1))
done <<< "$C"
echo
echo "Storm beats STOCK CRoaring on $ws/$tot;  beats FIXED CRoaring on $wf/$tot   (median of $REPS)"
echo "csv: $OUT"
