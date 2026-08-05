#!/usr/bin/env bash
# Formal benchmark run across the published Roaring corpora plus larger modern
# datasets (RESEARCH_PLAN.md 7.3, 14).
#
# Why 1:1 overlap with the Roaring papers matters
# -----------------------------------------------
# Every Storm-vs-CRoaring number before this was measured on OUR generator.
# A generator can encode the very structure the kernels exploit, so those
# numbers could not falsify the claim -- and "we picked favourable data" is the
# first objection a reviewer raises. The twelve real-roaring-datasets corpora
# are the files CRoaring's own harness runs by default (its README names
# census1881 as the default). Running on the incumbent's chosen data, with
# run_optimize() applied so it gets its run containers, removes that objection.
# Only then do the larger graphs mean anything as an EXTENSION of the map.
#
# The _srt variants are the row-sorted versions the Roaring papers report
# separately; sorting raises clustering, so keeping both gives two points on the
# clustering axis per corpus for free.
#
# Usage:  bench/run_corpora.sh [outdir]
set -euo pipefail
cd "$(dirname "$0")/.."

OUT=${1:-results/corpora}
SCRATCH=${SCRATCH:-/private/tmp/claude-501/-Volumes-SabrentM2-ML-projects-science-StormBitmaps/4d0fb93a-5e8d-46c3-aa16-fe051b5e5b1f/scratchpad}
PY=${PY:-.venv/bin/python}
BIN=build_portable
mkdir -p "$OUT"

ROWS=${ROWS:-256}
PAIRS=${PAIRS:-20000}
REPEATS=${REPEATS:-7}

host=$(uname -m)-$(uname -s)
echo "# host      $host"
echo "# rows      $ROWS   pairs $PAIRS   repeats $REPEATS"
echo "# out       $OUT"

# ---- corpus registry: name | source path | converter flags -----------------
# Tab-separated so paths with spaces survive; read -r keeps backslashes literal.
registry() {
cat <<'EOF'
census1881	rrd/census1881	--format csvdir
census1881_srt	rrd/census1881_srt	--format csvdir
census-income	rrd/census-income	--format csvdir
census-income_srt	rrd/census-income_srt	--format csvdir
weather_sept_85	rrd/weather_sept_85	--format csvdir
weather_sept_85_srt	rrd/weather_sept_85_srt	--format csvdir
wikileaks-noquotes	rrd/wikileaks-noquotes	--format csvdir
wikileaks-noquotes_srt	rrd/wikileaks-noquotes_srt	--format csvdir
uscensus2000	rrd/uscensus2000	--format csvdir
dimension_003	rrd/dimension_003	--format csvdir
dimension_008	rrd/dimension_008	--format csvdir
dimension_033	rrd/dimension_033	--format csvdir
as-skitter	as-skitter.txt	--format edgelist --symmetrize --min-card 2
wiki-Talk	wiki-Talk.txt	--format edgelist --min-card 2
soc-Pokec	pokec.txt	--format edgelist --min-card 2
com-LiveJournal	com-lj.txt	--format edgelist --symmetrize --min-card 2
com-Orkut	com-orkut.txt	--format edgelist --symmetrize --min-card 2
EOF
}

# ---- convert ----------------------------------------------------------------
registry | while IFS=$'\t' read -r name src flags; do
  [ -e "$SCRATCH/$src" ] || { echo "skip $name (no $src)"; continue; }
  bin="$SCRATCH/stormbin/$name.bin"
  mkdir -p "$SCRATCH/stormbin"
  if [ ! -f "$bin" ]; then
    echo "converting $name ..."
    # shellcheck disable=SC2086
    $PY tools/sets2bin.py "$SCRATCH/$src" -o "$bin" $flags > "$OUT/$name.triage.txt" 2>&1 \
      || { echo "  FAILED -- see $OUT/$name.triage.txt"; rm -f "$bin"; continue; }
  fi
  grep -E "universe|density|skew|sets" "$OUT/$name.triage.txt" | sed 's/^/    /'
done

# ---- run --------------------------------------------------------------------
# Stride so the row sample spans the corpus. Row order is never arbitrary:
# real-roaring files are attribute-ordered, graph vertex ids follow crawl order.
registry | while IFS=$'\t' read -r name src flags; do
  bin="$SCRATCH/stormbin/$name.bin"
  [ -f "$bin" ] || continue
  nrows=$($PY - "$bin" <<'PY'
import struct,sys
f=open(sys.argv[1],'rb'); f.read(8); print(struct.unpack('<IIII',f.read(16))[1])
PY
)
  stride=$(( nrows / ROWS )); [ "$stride" -lt 1 ] && stride=1
  echo "== $name  (rows=$nrows stride=$stride)"
  "$BIN/bench_baseline" --file "$bin" --rows "$ROWS" --in-stride "$stride" \
      --pairs "$PAIRS" --repeats "$REPEATS" --tag "$name" \
      > "$OUT/$name.txt" 2>&1 || { echo "   run FAILED"; continue; }
  tail -4 "$OUT/$name.txt" | sed 's/^/   /'
done

echo
echo "raw per-corpus output in $OUT/"
