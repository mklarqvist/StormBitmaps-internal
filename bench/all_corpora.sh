#!/usr/bin/env bash
# One clean sweep of every corpus through bench_real, so the "vs all-bitmap"
# column is comparable across datasets.
#
# Numbers reported piecemeal across §15-§22 came from different runs with
# different row counts and strides, which makes them incomparable. This produces
# the single table.
#
# Row count is chosen per corpus so the bitmap working set stays near a fixed
# budget: at universe 129.7M a row is 15.8 MB, so 512 rows would be 8 GB while
# at universe 2e5 it is 25 kB. Holding ROWS constant would therefore measure
# memory pressure rather than the kernels.
#
#   bench/all_corpora.sh [outfile]     # default results/all_corpora.csv
set -uo pipefail
cd "$(dirname "$0")/.."
OUT=${1:-results/all_corpora.csv}
BUDGET=${BUDGET:-1600000000}          # ~1.6 GB of bitmap per corpus
SP=${SCRATCH:-/private/tmp/claude-501/-Volumes-SabrentM2-ML-projects-science-StormBitmaps/4d0fb93a-5e8d-46c3-aa16-fe051b5e5b1f/scratchpad}
mkdir -p "$(dirname "$OUT")"

read -r -d '' CORPORA <<'EOF' || true
enwiki-categorylinks          2165205   IR-postings
dbpedia-link                  4384102   graph
wikipedia_link_en             5978043   graph
livejournal-groupmemberships  2197915   graph-bipartite
com-Orkut                     3004674   graph
com-LiveJournal               3212032   graph
soc-Pokec                     1277002   graph
as-skitter                    1478016   graph
wiki-Talk                     67492     graph
uscensus2000                  200       census
dimension_003                 15482     druid
dimension_008                 5240      druid
dimension_033                 173       druid
census1881                    200       census
census1881_srt                200       census
wikileaks-noquotes            200       text
wikileaks-noquotes_srt        200       text
weather_sept_85               200       sensor
weather_sept_85_srt           200       sensor
census-income                 200       census
census-income_srt             200       census
usher_sarscov2                29411     genomics
gnomad_chr21_exomes_af1e-3    625656    genomics
msprime_1M                    15592     genomics-sim
msprime_100k                  51062     genomics-sim
msprime_10k                   41991     genomics-sim
EOF

find_bin() { for d in data/corpora "$SP/stormbin"; do [ -f "$d/$1.bin" ] && { echo "$d/$1.bin"; return 0; }; done; return 1; }

echo "corpus,domain,universe,rows,mean_card,density,allbitmap_ns,best_cell,best_ns,speedup" > "$OUT"
printf "%-30s %-16s %12s %10s %11s %-8s %10s\n" corpus domain universe density "all-bmp ns" cell "vs bmp"

while read -r name total domain; do
  [ -z "$name" ] && continue
  bin=$(find_bin "$name") || { printf "%-30s  (absent)\n" "$name"; continue; }
  # universe from the header, then rows from the memory budget
  read -r uni <<< "$(.venv/bin/python -c "
import struct,sys
f=open('$bin','rb'); f.read(8); print(struct.unpack('<IIII',f.read(16))[2])")"
  rows=$(( BUDGET / (uni/8 + 1) )); [ "$rows" -gt 512 ] && rows=512; [ "$rows" -lt 64 ] && rows=64
  stride=$(( total / rows )); [ "$stride" -lt 1 ] && stride=1

  ./build_portable/bench_real --file "$bin" --rows "$rows" --stride "$stride" --tag "$name" 2>/dev/null \
  | .venv/bin/python -c "
import sys,re
t=sys.stdin.read()
g=lambda p,d=None:(lambda m:m.group(1) if m else d)(re.search(p,t,re.M))
uni=g(r'universe (\d+) bits') or '$uni'
dens=g(r'density ([\d.e-]+)'); mc=g(r'mean allele count ([\d.]+)')
ab=g(r'^B x B all-bitmap\s+([\d.]+)')
best=g(r'BEST: ([\d.]+) ns/pair, ([\d.]+)x');
cell=None
if ab:
    cells={}
    for ln in t.splitlines():
        m=re.match(r'^(B x [BSRW]|S x S|R x R|B x B zone-mapped)\s+([\d.]+)',ln)
        if m: cells[m.group(1)]=float(m.group(2))
    if cells: cell=min(cells,key=cells.get)
bn=g(r'BEST: ([\d.]+) ns'); sp=g(r'([\d.]+)x over all-bitmap')
if not (ab and bn and sp): print('%-30s  (parse failed)'%'$name'); sys.exit()
print('%-30s %-16s %12s %10s %11.1f %-8s %9sx'%('$name','$domain',format(int(uni),','),dens or '?',float(ab),
      (cell or '?').replace('B x B zone-mapped','BxB-zm').replace(' ',''),sp))
open('$OUT','a').write('%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n'%('$name','$domain',uni,'$rows',mc or '',dens or '',ab,(cell or ''),bn,sp))
"
done <<< "$CORPORA"
echo; echo "csv: $OUT"
