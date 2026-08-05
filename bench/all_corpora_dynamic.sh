#!/usr/bin/env bash
# Speedup from ONLINE selection -- what a caller actually gets.
#
# bench/all_corpora.sh reported the best SINGLE cell applied to every pair. On a
# heterogeneous corpus that is meaningless: on msprime_1M, B x S is 843x faster
# than B x B on pairs with |S|<=10 and 18x SLOWER on pairs with |S|>1e5. No fixed
# choice is right, and quoting the best fixed choice understates the design and
# misrepresents what it does.
#
# This reports per-tile and probe-and-commit -- both decide at runtime from the
# data -- against all-bitmap.
set -uo pipefail
cd "$(dirname "$0")/.."
OUT=${1:-results/all_corpora_dynamic.csv}
ZMFLAG=${ZMFLAG:-}
BUDGET=${BUDGET:-1600000000}
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
echo "corpus,domain,universe,rows,allbitmap_ns,pertile_ns,probe_ns,pertile_x,probe_x,sel_pct" > "$OUT"
printf "%-30s %-12s %12s %13s %11s %9s %9s\n" corpus domain universe "all-bitmap ns" "best dyn ns" "per-tile" "probe"
while read -r n tot dom; do
  [ -z "$n" ] && continue
  b=$(fb "$n") || continue
  u=$(.venv/bin/python -c "
import struct;f=open('$b','rb');f.read(8);print(struct.unpack('<IIII',f.read(16))[2])")
  r=$(( BUDGET / (u/8 + 1) )); [ $r -gt 512 ] && r=512; [ $r -lt 64 ] && r=64
  s=$(( tot / r )); [ $s -lt 1 ] && s=1
  ./build_portable/bench_allpairs --file "$b" --rows $r --in-stride $s --tile 64 $ZMFLAG --tag "$n" 2>/dev/null \
  | .venv/bin/python -c "
import sys,re
t=sys.stdin.read()
g=lambda p:(lambda m:m.group(1) if m else None)(re.search(p,t,re.M))
ab=g(r'^all-bitmap\s+([\d.]+)'); pt=g(r'^per-tile\s+([\d.]+)'); pb=g(r'^probe\s+([\d.]+)')
sp=g(r'probe\s+([\d.]+)%')
if not(ab and pt and pb): print('%-30s (parse failed)'%'$n'); sys.exit()
ab,pt,pb=float(ab),float(pt),float(pb)
print('%-30s %-12s %12s %11.1f %11.1f %8.2fx %8.2fx'%('$n','$dom',format(int($u),','),ab,pb,ab/pt,ab/pb))
open('$OUT','a').write('%s,%s,%s,%s,%.3f,%.3f,%.3f,%.4f,%.4f,%s\n'%('$n','$dom',$u,'$r',ab,pt,pb,ab/pt,ab/pb,sp or ''))
"
done <<< "$C"
