#!/usr/bin/env python3
"""Stream-parse the enwiki categorylinks MySQL dump into a sets2bin.py adjacency file.

Why this exists: the classic categorylinks schema (cl_from int, cl_to varbinary
category-name) was replaced (MediaWiki category-normalization migration,
T301930) with cl_target_id, a foreign key into the `category` table's cat_id.
tools/sets2bin.py has no reader for this bipartite (category x page) MySQL
dump shape, so this script does the SQL-tuple parsing and emits the plain
"adjacency" text format sets2bin.py already understands: one category per
line, space-separated page ids, ordered by descending cardinality so that
`sets2bin.py --max-rows N` keeps the N LARGEST categories, not just the first
N in file order.

categorylinks.sql.gz row shape (7 fields per tuple), confirmed against a live
sample:
    (cl_from, cl_sortkey, cl_timestamp, cl_sortkey_prefix, cl_type,
     cl_collation_id, cl_target_id)
cl_sortkey is an arbitrary escaped binary blob (collation weight bytes) that
can itself contain literal '(', ')', ',' bytes UNESCAPED -- mysqldump only
backslash-escapes quotes/backslashes/control chars, not parens or commas. A
naive text split on "),(" WILL corrupt rows whose sortkey happens to contain
that byte sequence. The tuple regex below matches the quoted fields with
'(?:[^'\\]|\\.)*', which consumes escaped content correctly regardless of
what raw bytes it contains, so tuple boundaries are never guessed from text
splitting.

category.sql.gz row shape (5 fields): (cat_id, cat_title, cat_pages,
cat_subcats, cat_files). Used ONLY to resolve a few cat_id -> title pairs for
the human-readable sample printout; the corpus itself never stores names.
"""
import argparse
import gzip
import re
import struct
import sys
import time

import numpy as np

CHUNK = 1 << 25  # 32 MiB of DECOMPRESSED bytes per read

# 7-field categorylinks tuple. Groups: (cl_from, cl_target_id).
_Q = rb"'(?:[^'\\]|\\.)*'"
CL_PATTERN = re.compile(
    rb"\((\d+)," + _Q + b"," + _Q + b"," + _Q + b"," + _Q + rb",\d+,(\d+)\)",
    re.DOTALL,
)

# 5-field category tuple. Groups: (cat_id, cat_title).
CAT_PATTERN = re.compile(
    rb"\((\d+),('(?:[^'\\]|\\.)*'),\d+,\d+,\d+\)",
    re.DOTALL,
)

_ESCAPES = {
    b"0": b"\x00", b"n": b"\n", b"r": b"\r", b"t": b"\t",
    b"Z": b"\x1a", b"\\": b"\\", b"'": b"'", b'"': b'"',
}


def _unescape(q):
    """Undo mysqldump backslash escaping on a quoted-and-stripped byte string."""
    out = bytearray()
    i = 0
    n = len(q)
    while i < n:
        c = q[i]
        if c == 0x5C and i + 1 < n:  # backslash
            nxt = q[i + 1:i + 2]
            out += _ESCAPES.get(nxt, nxt)
            i += 2
        else:
            out.append(c)
            i += 1
    return bytes(out)


def _iter_gz_chunks(path):
    with gzip.open(path, "rb") as gz:
        while True:
            chunk = gz.read(CHUNK)
            if not chunk:
                return
            yield chunk


def parse_category_titles(path, wanted_ids):
    """Return {cat_id: title_str} for cat_id in wanted_ids (a set)."""
    wanted = set(wanted_ids)
    out = {}
    buf = b""
    t0 = time.time()
    n_bytes = 0
    for chunk in _iter_gz_chunks(path):
        n_bytes += len(chunk)
        buf += chunk
        matches = list(CAT_PATTERN.finditer(buf))
        if matches:
            for m in matches:
                cid = int(m.group(1))
                if cid in wanted:
                    title = _unescape(m.group(2)[1:-1]).decode("utf-8", "replace")
                    out[cid] = title
            buf = buf[matches[-1].end():]
        if len(out) == len(wanted):
            break
    print(f"# category.sql.gz: scanned {n_bytes/1e6:.0f} MB decompressed, "
          f"resolved {len(out)}/{len(wanted)} titles in {time.time()-t0:.1f}s",
          file=sys.stderr)
    return out


def parse_categorylinks(path, scratch_from, scratch_target):
    """Stream categorylinks.sql.gz -> two raw uint32 files (cl_from, cl_target_id)."""
    t0 = time.time()
    n_rows = 0
    n_bytes = 0
    buf = b""
    sample = []
    with open(scratch_from, "wb") as ff, open(scratch_target, "wb") as ft:
        for chunk in _iter_gz_chunks(path):
            n_bytes += len(chunk)
            buf += chunk
            matches = CL_PATTERN.finditer(buf)
            froms = []
            targets = []
            last_end = 0
            for m in matches:
                froms.append(int(m.group(1)))
                targets.append(int(m.group(2)))
                last_end = m.end()
                if len(sample) < 10:
                    sample.append((int(m.group(1)), int(m.group(2))))
            if last_end:
                buf = buf[last_end:]
            if froms:
                a = np.array(froms, dtype=np.uint32)
                b = np.array(targets, dtype=np.uint32)
                a.tofile(ff)
                b.tofile(ft)
                n_rows += len(froms)
            elapsed = time.time() - t0
            print(f"# categorylinks: {n_bytes/1e9:.2f} GB decompressed, "
                  f"{n_rows/1e6:.1f}M rows, {elapsed:.0f}s elapsed",
                  file=sys.stderr)
    print(f"# categorylinks: DONE. {n_rows:,} rows in {time.time()-t0:.1f}s",
          file=sys.stderr)
    return n_rows, sample


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("categorylinks_gz")
    ap.add_argument("--category-gz", help="category.sql.gz, for title samples only")
    ap.add_argument("-o", "--adjacency-out", required=True,
                     help="output adjacency text file (one category per line, "
                          "space-separated page ids, sorted by descending size)")
    ap.add_argument("--scratch-dir", required=True)
    ap.add_argument("--min-card", type=int, default=2)
    a = ap.parse_args()

    scratch_from = f"{a.scratch_dir}/_cl_from.u32"
    scratch_target = f"{a.scratch_dir}/_cl_target.u32"

    n_rows, sample = parse_categorylinks(a.categorylinks_gz, scratch_from, scratch_target)

    print("# --- 10 sample parsed (page_id, cl_target_id) pairs (raw ids) ---")
    for pid, tid in sample:
        print(f"#   page_id={pid}  cl_target_id={tid}")

    print("# loading scratch pairs into memory for grouping ...", file=sys.stderr)
    cl_from = np.fromfile(scratch_from, dtype=np.uint32)
    cl_target = np.fromfile(scratch_target, dtype=np.uint32)
    assert cl_from.size == cl_target.size == n_rows

    universe = int(cl_from.max()) + 1
    print(f"# distinct (cl_from,cl_target) pairs: {n_rows:,}  "
          f"page-id universe (max cl_from + 1): {universe:,}", file=sys.stderr)

    print("# grouping by cl_target_id (np.unique) ...", file=sys.stderr)
    uniq_targets, inverse, counts = np.unique(cl_target, return_inverse=True, return_counts=True)
    K = uniq_targets.size
    print(f"# distinct categories (cl_target_id values): {K:,}", file=sys.stderr)

    print("# lexsort (category, page_id) ...", file=sys.stderr)
    order = np.lexsort((cl_from, inverse))
    sorted_pages = cl_from[order]
    offs = np.zeros(K + 1, dtype=np.int64)
    np.cumsum(counts, out=offs[1:])

    keep = np.nonzero(counts >= a.min_card)[0]
    print(f"# categories with >= {a.min_card} pages: {keep.size:,} of {K:,}", file=sys.stderr)

    # Sort kept categories by descending size so a downstream --max-rows N
    # keeps the N largest, not an arbitrary prefix.
    keep_sorted = keep[np.argsort(-counts[keep], kind="stable")]

    # Resolve a few titles for the human-readable sample, if category.sql.gz given.
    titles = {}
    if a.category_gz:
        top_ids = [int(uniq_targets[i]) for i in keep_sorted[:10]]
        titles = parse_category_titles(a.category_gz, top_ids)

    print("# --- top 10 categories by page count (post cl_target_id -> title resolution) ---")
    for rank, i in enumerate(keep_sorted[:10]):
        tid = int(uniq_targets[i])
        name = titles.get(tid, "<unresolved>")
        print(f"#   #{rank+1}  cl_target_id={tid}  pages={int(counts[i]):,}  title={name}")

    print(f"# writing adjacency file {a.adjacency_out} "
          f"({keep_sorted.size:,} categories, descending size) ...", file=sys.stderr)
    t0 = time.time()
    with open(a.adjacency_out, "w") as f:
        for i in keep_sorted:
            s, e = offs[i], offs[i + 1]
            row = np.unique(sorted_pages[s:e])
            f.write(" ".join(map(str, row.tolist())))
            f.write("\n")
    print(f"# adjacency file written in {time.time()-t0:.1f}s", file=sys.stderr)
    print(f"# universe(max page id + 1)={universe}", file=sys.stderr)


if __name__ == "__main__":
    main()
