#!/usr/bin/env python3
"""Convert an arbitrary sparse set collection into STORMBIN.

Until now the only real-data path was VCF-specific (tools/gt2bin.py), which
pinned the sole real anchor to human variants -- exactly the domain measured to
sit OUTSIDE the target regime (large universe XOR sparsity; see the chr20
finding, density 3.14% haplotype-major, 0.93x vs all-bitmap). This reads the
formats the candidate non-genomic corpora actually ship in, so a downloaded
dataset can be triaged and measured the same day.

Target regime, restated so --stats can check it mechanically:
    universe m >= 1e6   AND   mean density <= 1e-3   AND   skewed cardinality.

Input formats
    edgelist   "u v" per line, '#'/'%' comments skipped (SNAP, Network
               Repository, LAW). Rows are vertex neighbourhoods, universe |V|.
    adjacency  one set per line, whitespace-separated ints (posting lists,
               market-basket, FIMI).
    csvdir     a DIRECTORY, one file per set, comma-separated ids. This is the
               real-roaring-datasets layout that CRoaring's own benchmark
               harness consumes, so we can run on the incumbent's own corpora.

STORMBIN layout (matches tools/gt2bin.py and bench/bench_real.cpp)
    b'STORMBIN' <IIII>(version=1, n_rows, n_bits, pad)
    then per row: <I>(count) followed by count little-endian u32, ASCENDING.

The ascending-and-deduplicated guarantee is load-bearing: build_row() feeds the
positions straight into the sorted-array and run representations, so an
unsorted or duplicated row silently corrupts S, R and W while leaving B correct
-- which shows up as a kernel disagreement, not as a parse error.
"""
import argparse
import struct
import sys

import numpy as np

CHUNK = 1 << 22  # bytes per read; parsing cost is dominated by np.fromstring-ish work


def _parse_block(block):
    """Parse a whitespace-separated 2-column int block into an (n,2) array.

    Deliberately NOT a per-line Python loop with int(): com-LiveJournal is 34.7M
    edges and com-Orkut 117M, where building Python int objects costs both
    minutes and gigabytes (~28 bytes per int, two per edge, doubled again by
    symmetrisation). numpy converts a list of bytes tokens to int64 in one C
    pass, so the only Python-level work is the split.
    """
    lines = [ln for ln in block.split(b"\n")
             if ln and ln[0:1] not in (b"#", b"%")]
    if not lines:
        return np.empty((0, 2), dtype=np.int64)
    toks = b" ".join(lines).split()
    if len(toks) % 2:
        # A trailing partial record would silently shift every subsequent pair
        # by one column, turning the whole graph into garbage that still parses.
        raise ValueError("edge list block has an odd token count; "
                         "expected exactly 2 columns per line")
    return np.array(toks, dtype=np.int64).reshape(-1, 2)


def _parse_edgelist(path, symmetrize):
    """Stream an edge list into two parallel int64 arrays."""
    parts = []
    with open(path, "rb") as f:
        tail = b""
        while True:
            buf = f.read(CHUNK)
            if not buf:
                break
            buf = tail + buf
            nl = buf.rfind(b"\n")
            if nl < 0:
                tail = buf
                continue
            tail, block = buf[nl + 1:], buf[:nl]
            parts.append(_parse_block(block))
        if tail.strip():
            parts.append(_parse_block(tail))

    if not parts:
        return np.empty(0, dtype=np.int64), np.empty(0, dtype=np.int64)
    e = np.concatenate(parts)
    del parts
    u, v = e[:, 0].copy(), e[:, 1].copy()
    del e
    if symmetrize:
        u, v = np.concatenate([u, v]), np.concatenate([v, u])
    return u, v


def _rows_from_edges(u, v, universe):
    """Group destination ids by source id. Returns (offsets, sorted_targets)."""
    # bincount is order-independent, so the offsets are valid before any sort.
    counts = np.bincount(u, minlength=universe)
    offs = np.zeros(universe + 1, dtype=np.int64)
    np.cumsum(counts, out=offs[1:])
    # One lexsort does both jobs: primary key u groups the rows in the same
    # order the offsets assume, secondary key v sorts within each row, which the
    # sorted-array and run views require. Keying on u*universe+v would be one
    # pass but overflows int64 once |V| exceeds ~3e9.
    return offs, v[np.lexsort((v, u))]


def _iter_adjacency(path):
    with open(path, "rb") as f:
        for line in f:
            if not line.strip() or line[0:1] in (b"#", b"%"):
                continue
            yield np.array([int(x) for x in line.split()], dtype=np.int64)


def _natkey(p):
    """Sort census1881.csv2.txt before census1881.csv10.txt.

    Plain lexicographic order would interleave them, which does not change any
    per-pair result but does change WHICH pairs a strided sample draws -- so
    without this the corpus is not reproducible across machines whose readdir
    order differs.
    """
    import re
    return [int(t) if t.isdigit() else t for t in re.split(r"(\d+)", p.name)]


def _iter_csvdir(path):
    """One file per set, comma-separated ids (the real-roaring-datasets layout).

    This is the corpus CRoaring's own benchmark harness runs by default, so
    reading it natively is what makes the head-to-head an exact match rather
    than an analogy.
    """
    from pathlib import Path
    files = sorted((p for p in Path(path).rglob("*") if p.is_file()), key=_natkey)
    for p in files:
        raw = p.read_bytes().replace(b"\n", b",").strip(b",")
        if not raw.strip():
            yield np.empty(0, dtype=np.int64)
            continue
        yield np.array([int(x) for x in raw.split(b",") if x.strip()], dtype=np.int64)


def _skew(cards):
    """Report the cardinality spectrum in log2 bins plus a Gini-ish summary."""
    nz = cards[cards > 0]
    if nz.size == 0:
        return "empty", []
    b = np.floor(np.log2(nz)).astype(int)
    hist = np.bincount(b, minlength=1)
    s = np.sort(nz)
    frac_top1 = s[int(0.99 * s.size):].sum() / s.sum()
    return f"top 1% of sets hold {100*frac_top1:.1f}% of all elements", hist


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input")
    ap.add_argument("-o", "--output", help="STORMBIN path; omit with --stats to only triage")
    ap.add_argument("--format", choices=("edgelist", "adjacency", "csvdir"), default="edgelist")
    ap.add_argument("--symmetrize", action="store_true",
                    help="treat an edge list as undirected (add both directions)")
    ap.add_argument("--universe", type=int, default=0,
                    help="force universe size; default is max id + 1")
    ap.add_argument("--min-card", type=int, default=1,
                    help="drop sets smaller than this (degree-1 vertices are noise)")
    ap.add_argument("--max-rows", type=int, default=0, help="cap rows written (0 = all)")
    ap.add_argument("--stats", action="store_true", help="print regime triage and exit")
    a = ap.parse_args()

    if a.format == "edgelist":
        u, v = _parse_edgelist(a.input, a.symmetrize)
        if u.size == 0:
            sys.exit("no edges parsed")
        universe = a.universe or int(max(u.max(), v.max())) + 1
        offs, tgt = _rows_from_edges(u, v, universe)
        cards = np.diff(offs)

        def row_at(i):
            s, e = offs[i], offs[i + 1]
            return np.unique(tgt[s:e])
        n_src = universe
    else:
        src = _iter_csvdir(a.input) if a.format == "csvdir" else _iter_adjacency(a.input)
        rows = [np.unique(r) for r in src]
        universe = a.universe or int(max((r.max() for r in rows if r.size), default=-1)) + 1
        cards = np.array([r.size for r in rows], dtype=np.int64)

        def row_at(i):
            return rows[i]
        n_src = len(rows)

    keep = np.nonzero(cards >= a.min_card)[0]
    if a.max_rows:
        keep = keep[:a.max_rows]
    kept_cards = cards[keep]
    mean_card = kept_cards.mean() if kept_cards.size else 0.0
    density = mean_card / universe if universe else 0.0

    note, _ = _skew(kept_cards)
    print(f"# source          {a.input}")
    print(f"# universe m      {universe:,}")
    print(f"# sets (kept)     {keep.size:,} of {n_src:,}")
    print(f"# mean |Xi|       {mean_card:,.1f}")
    print(f"# mean density    {density:.3e}")
    print(f"# max |Xi|        {int(kept_cards.max()) if kept_cards.size else 0:,}")
    print(f"# skew            {note}")
    ok_m = universe >= 1_000_000
    ok_d = density <= 1e-3
    print(f"# REGIME          universe>=1e6: {'PASS' if ok_m else 'FAIL'}   "
          f"density<=1e-3: {'PASS' if ok_d else 'FAIL'}   "
          f"=> {'QUALIFIES' if (ok_m and ok_d) else 'OUT OF REGIME'}")

    if a.stats or not a.output:
        return

    with open(a.output, "wb") as f:
        f.write(b"STORMBIN" + struct.pack("<IIII", 1, 0, 0, 0))
        for i in keep:
            r = row_at(int(i)).astype(np.uint32)
            f.write(struct.pack("<I", r.size))
            f.write(r.tobytes())
        f.seek(8)
        f.write(struct.pack("<IIII", 1, int(keep.size), int(universe), 0))
    print(f"# wrote           {a.output}")


if __name__ == "__main__":
    main()
