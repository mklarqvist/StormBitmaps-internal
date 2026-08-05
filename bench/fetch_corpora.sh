#!/usr/bin/env bash
# Fetch the benchmark corpora. No dataset is committed to this repository.
#
# See data/README.md for what each corpus is, its measured shape, its citation,
# and how to reconstruct it if a URL rots. That file is the durable record; this
# script is only the convenience wrapper.
#
#   bench/fetch_corpora.sh              # everything except com-Orkut (~750 MB)
#   bench/fetch_corpora.sh --with-orkut # add com-Orkut (+427 MB, ~10 GB RAM to convert)
#   bench/fetch_corpora.sh --roaring    # only the CRoaring corpora (~50 MB)
#
# Destination defaults to $SCRATCH, or ./data/corpora if unset. Datasets are
# large and are deliberately kept out of the working tree.
set -euo pipefail
cd "$(dirname "$0")/.."

DEST=${SCRATCH:-$PWD/data/corpora}
WANT_ORKUT=0
ONLY_ROARING=0
for a in "$@"; do
  case "$a" in
    --with-orkut) WANT_ORKUT=1 ;;
    --roaring)    ONLY_ROARING=1 ;;
    --dest)       shift; DEST=$1 ;;
    -h|--help)    sed -n '2,14p' "$0"; exit 0 ;;
  esac
done
mkdir -p "$DEST"
echo "destination: $DEST"

have() { [ -s "$1" ]; }

# ---------------------------------------------------------------- group 1
# real-roaring-datasets: the corpora CRoaring's own harness runs by default.
# Cloned rather than fetched file-by-file because the repo is the citable
# artifact and a shallow clone is smaller than the sum of the zips.
if [ ! -d "$DEST/rrd/.git" ]; then
  echo "== cloning real-roaring-datasets"
  git clone --depth 1 https://github.com/RoaringBitmap/real-roaring-datasets.git \
      "$DEST/rrd"
else
  echo "== real-roaring-datasets present"
fi
for z in "$DEST"/rrd/*.zip; do
  d="${z%.zip}"
  [ -d "$d" ] || { echo "   unzip $(basename "$z")"; unzip -oq "$z" -d "$d"; }
done

[ "$ONLY_ROARING" = 1 ] && { echo "done (CRoaring corpora only)"; exit 0; }

# ---------------------------------------------------------------- group 2
# SNAP graphs. Note com-* live under bigdata/communities/, not the top-level
# data/ path -- a URL shape that has changed before.
SNAP=https://snap.stanford.edu/data
fetch() {  # fetch <url> <outfile> <label>
  if have "$DEST/$2"; then echo "== $3 present"; return; fi
  echo "== fetching $3"
  curl -fSL --retry 3 --retry-delay 2 -o "$DEST/$2.part" "$1" || {
      echo "   FAILED: $1"; echo "   see data/README.md to re-identify this corpus";
      rm -f "$DEST/$2.part"; return 1; }
  mv "$DEST/$2.part" "$DEST/$2"
}
gunz() { have "$DEST/$2" || { echo "   gunzip -> $2"; gzip -dc "$DEST/$1" > "$DEST/$2"; }; }

fetch "$SNAP/as-skitter.txt.gz"                            as-skitter.txt.gz  "as-skitter (32 MB)"
fetch "$SNAP/wiki-Talk.txt.gz"                             wiki-Talk.txt.gz   "wiki-Talk (16 MB)"
fetch "$SNAP/soc-pokec-relationships.txt.gz"               pokec.txt.gz       "soc-Pokec (126 MB)"
fetch "$SNAP/bigdata/communities/com-lj.ungraph.txt.gz"    comlj.txt.gz       "com-LiveJournal (119 MB)"
[ "$WANT_ORKUT" = 1 ] && \
fetch "$SNAP/bigdata/communities/com-orkut.ungraph.txt.gz" com-orkut.txt.gz   "com-Orkut (427 MB)"

gunz as-skitter.txt.gz as-skitter.txt
gunz wiki-Talk.txt.gz  wiki-Talk.txt
gunz pokec.txt.gz      pokec.txt
gunz comlj.txt.gz      com-lj.txt
[ "$WANT_ORKUT" = 1 ] && gunz com-orkut.txt.gz com-orkut.txt

cat <<EOF

fetched into $DEST

next:
  SCRATCH=$DEST bench/run_corpora.sh results/corpora
  .venv/bin/python bench/summarize_corpora.py results/corpora

com-Orkut is excluded by default: +427 MB download and roughly 10 GB of RAM to
symmetrise 117M edges. Pass --with-orkut to include it.
EOF
