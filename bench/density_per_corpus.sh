#!/usr/bin/env bash
# One full density sweep PER CORPUS, at that corpus's own universe size.
#
# bench/density_sweep.sh pins the universe at 65,536 so cache residency does not
# drift along the x axis. That is right for a single curve, but it means the
# published curve describes one universe only -- and this project's corpora span
# 2.0e5 to 1.3e8, four orders of magnitude, across which the crossover points
# move (C18: the same structure tunes oppositely in different regimes; C34c: the
# tile transpose fits P-L2 at one width and not another).
#
# So: sweep the whole density axis separately for each corpus's universe. Within
# a plot the universe is still pinned, so residency does not drift along x --
# only ACROSS plots, which is the comparison being made.
#
# Grid: 1 bit set, 98 linear steps, m-1 bits set. The extremes are included
# because that is where a Theta(m) kernel is most exposed and where the sparse
# and complement cells are at their best.
#
#   bench/density_per_corpus.sh [outfile]
set -uo pipefail
cd "$(dirname "$0")/.."
OUT=${1:-results/density/per_corpus.jsonl}
BUDGET=${BUDGET:-1000000000}     # ~1 GB of bitmap per point
STEPS=${STEPS:-98}
mkdir -p "$(dirname "$OUT")"
: > "$OUT"
[ -x build_portable/bench_cells ] || { echo "build bench_cells first"; exit 1; }

# corpus universe  -- the real universes, so each sweep matches a real corpus
read -r -d '' U <<'EOF' || true
census-income 199523
msprime_100k 200000
weather_sept_85 1015367
wikileaks-noquotes 1353179
gnomad_chr21 1461894
as-skitter 1696415
msprime_1M 2000000
wiki-Talk 2394385
com-Orkut 3072627
dimension_003 3866847
census1881 4277806
com-LiveJournal 4036538
livejournal-groupmemberships 7489074
usher_sarscov2 8451771
wikipedia_link_en 11206012
dbpedia-link 18268993
uscensus2000 36974578
enwiki-categorylinks 129698523
EOF

while read -r name m; do
  [ -z "$name" ] && continue
  rows=$(( BUDGET / (m/8 + 1) ))
  [ "$rows" -gt 256 ] && rows=256
  [ "$rows" -lt 16 ] && rows=16
  printf "%-30s m=%-12s rows=%-4s " "$name" "$m" "$rows"
  # 1 bit, 98 linear steps, m-1 bits. Expressed as densities so bench_cells can
  # take them directly; the endpoints are exact rather than rounded into the grid.
  ds=$(.venv/bin/python -c "
m=$m; n=$STEPS
lo, hi = 1.0/m, (m-1.0)/m
print(' '.join('%.10g'%(lo+(hi-lo)*i/(n+1)) for i in range(n+2)))")
  tmp=$(mktemp); k=0
  for d in $ds; do
    : > "$tmp"
    ./build_portable/bench_cells --rows "$rows" --universe "$m" --density "$d" \
        --structure clustered --spectrum uniform --json "$tmp" >/dev/null 2>&1 || true
    if [ -s "$tmp" ]; then
      # bench_cells already records universe and density; only the corpus tag is
      # missing, and it is what lets one file hold every sweep.
      .venv/bin/python -c "
import sys, json
for ln in open('$tmp'):
    ln=ln.strip()
    if not ln.startswith('{'): continue
    try: o=json.loads(ln)
    except Exception: continue
    o['corpus']='$name'
    print(json.dumps(o))
" >> "$OUT" && k=$((k+1))
    fi
  done
  rm -f "$tmp"
  echo "points=$k"
done <<< "$U"

echo
echo "jsonl: $OUT   ($(wc -l < "$OUT") rows)"
echo "plot:  .venv/bin/python bench/plot_density_per_corpus.py $OUT"
