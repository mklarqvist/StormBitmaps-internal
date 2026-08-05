#!/usr/bin/env bash
# WITH / WITHOUT the overlap filter, on every corpus.
#
# The filter is OPTIONAL. Nothing in the design requires it, the gate declines it
# on a third of corpora, and the honest way to present that is to run everything
# both ways rather than to report only the configuration that wins. This produces
# the whole table: unfiltered kernel, filtered kernel, and the gated choice
# between them.
#
#   bench/filter_ablation.sh [outfile]      # default results/ablation/filter.csv
#
# Env: BITS (filter width, default 1048576), REPEATS (9), ROWS (256)
set -uo pipefail
cd "$(dirname "$0")/.."

OUT=${1:-results/ablation/filter.csv}
BITS=${BITS:-1048576}; REPEATS=${REPEATS:-9}; ROWS=${ROWS:-256}
SP=${SCRATCH:-/private/tmp/claude-501/-Volumes-SabrentM2-ML-projects-science-StormBitmaps/4d0fb93a-5e8d-46c3-aa16-fe051b5e5b1f/scratchpad}
mkdir -p "$(dirname "$OUT")"
[ -x build_portable/bench_bloom ] || { echo "build bench_bloom first"; exit 1; }

# name  path  total_rows_in_file
# Two corpus roots: the original 17 live in the scratchpad, the newly fetched
# large ones in data/corpora. Both are searched so this stays one table.
read -r -d '' CORPORA <<'EOF' || true
as-skitter                    1478016
com-LiveJournal               3212032
com-Orkut                     3004674
soc-Pokec                     1277002
wiki-Talk                     67492
dimension_003                 15482
dimension_008                 5240
dimension_033                 173
uscensus2000                  200
census1881                    200
census1881_srt                200
wikileaks-noquotes            200
wikileaks-noquotes_srt        200
weather_sept_85               200
weather_sept_85_srt           200
census-income                 200
census-income_srt             200
dbpedia-link                  4384102
wikipedia_link_en             5978043
livejournal-groupmemberships  2197915
usher_sarscov2                29411
gnomad_chr21_exomes_af1e-3    625656
msprime_10k                   41991
msprime_100k                  51062
msprime_1M                    15592
EOF

find_bin() {
  for d in "$SP/stormbin" data/corpora; do
    [ -f "$d/$1.bin" ] && { echo "$d/$1.bin"; return 0; }
  done
  return 1
}

echo "corpus,universe,rows,mean_card,density,disjoint_pct,no_filter_ns,filter_ns,gated_ns,filter_speedup,gated_speedup,gate,correct" > "$OUT"
printf "%-30s %10s %10s %10s %8s %8s  %s\n" corpus "no-filter" "filter" "gated" "filt x" "gated x" gate

while read -r name total; do
  [ -z "$name" ] && continue
  bin=$(find_bin "$name") || { printf "%-30s  (not present)\n" "$name"; continue; }
  stride=$(( total / ROWS )); [ "$stride" -lt 1 ] && stride=1

  o=$(./build_portable/bench_bloom --file "$bin" --rows "$ROWS" --stride "$stride" \
        --bits "$BITS" --repeats "$REPEATS" --tag "$name" 2>/dev/null)
  [ -z "$o" ] && { printf "%-30s  (run failed)\n" "$name"; continue; }

  echo "$o" | .venv/bin/python -c "
import sys, re
t = sys.stdin.read()
g = lambda p, d=None: (lambda m: m.group(1) if m else d)(re.search(p, t, re.M))
uni  = g(r'universe=(\d+)')
dis  = g(r'disjoint=([\d.]+)%')
nof  = g(r'^B x S ilp8 \(no filter\)\s+([\d.]+)')
filt = g(r'^coarse fixed\s+([\d.]+)')
aut  = g(r'^AUTO \(gated\)\s+([\d.]+)')
gate = g(r'-> (USE w=\d+|bypass|filter pipeline|bypass to plain kernel)', 'n/a')
ok   = 0 if 'WRONG' in t or 'MISMATCH' in t else 1
if not (nof and filt and aut):
    print('%-30s  (parse failed)' % '$name'); sys.exit()
nof, filt, aut = float(nof), float(filt), float(aut)
print('%-30s %10.3f %10.3f %10.3f %7.2fx %7.2fx  %s%s' %
      ('$name', nof, filt, aut, nof/filt, nof/aut, gate, '' if ok else '  WRONG'))
open('$OUT','a').write('%s,%s,%s,,,%s,%.4f,%.4f,%.4f,%.4f,%.4f,%s,%d\n' %
      ('$name', uni or '', '$ROWS', dis or '', nof, filt, aut, nof/filt, nof/aut, gate, ok))
"
done <<< "$CORPORA"

echo
echo "csv: $OUT"
