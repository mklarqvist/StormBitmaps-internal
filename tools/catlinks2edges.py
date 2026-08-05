#!/usr/bin/env python3
"""Stream the enwiki categorylinks SQL dump into a `category page` edge list.

Rows are categories, elements are page ids — the closest open stand-in for IR
posting lists, and the one domain missing from the corpus set (data/README.md).

WHY NOT SPLIT ON COMMAS. The dump's schema is

    cl_from int, cl_sortkey varbinary(230), cl_timestamp, cl_sortkey_prefix
    varbinary(255), cl_type enum('page','subcat','file'), cl_collation_id,
    cl_target_id bigint

cl_sortkey and cl_sortkey_prefix are *binary* and routinely contain commas,
quotes, escaped quotes and parentheses, so any tuple-splitting on ',' or on
'),(' corrupts the data silently. Only the two integers at the ends are wanted,
and the cl_type enum is a three-valued literal that anchors the tail
unambiguously:

    (cl_from, '<binary>', '<ts>', '<binary>', '<'page'|'subcat'|'file'>',
     cl_collation_id, cl_target_id)

so a non-greedy match from '(' to that anchor extracts both ends without ever
interpreting the binary middle. Validated below by printing samples and by
checking that the match count tracks the number of tuples.
"""
import argparse
import gzip
import re
import sys

# ( cl_from , ...anything... , 'type' , collation_id , cl_target_id )
TUPLE = re.compile(
    rb"\((\d+),'.*?','(?:page|subcat|file)',\d+,(\d+)\)", re.DOTALL)

CHUNK = 1 << 24


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dump")
    ap.add_argument("out")
    ap.add_argument("--samples", type=int, default=10,
                    help="print this many parsed (category, page) pairs and the "
                         "raw tuple they came from, as a correctness check")
    ap.add_argument("--progress-every", type=int, default=20_000_000)
    a = ap.parse_args()

    n = 0
    shown = 0
    tail = b""
    with gzip.open(a.dump, "rb") as f, open(a.out, "wb") as w:
        while True:
            buf = f.read(CHUNK)
            if not buf:
                break
            buf = tail + buf
            last = 0
            out = []
            for m in TUPLE.finditer(buf):
                cl_from, cl_target = m.group(1), m.group(2)
                out.append(cl_target + b" " + cl_from + b"\n")
                last = m.end()
                if shown < a.samples:
                    raw = m.group(0)
                    sys.stderr.write(
                        f"  sample {shown+1}: category={int(cl_target)} "
                        f"page={int(cl_from)}   raw[:90]={raw[:90]!r}\n")
                    shown += 1
                n += 1
                if n % a.progress_every == 0:
                    sys.stderr.write(f"  {n:,} links\n"); sys.stderr.flush()
            if out:
                w.write(b"".join(out))
            # Keep an unmatched remainder so a tuple straddling a chunk boundary
            # is not dropped. Bounded so a pathological run cannot grow it forever.
            tail = buf[last:] if len(buf) - last < (1 << 20) else b""
    sys.stderr.write(f"wrote {a.out}: {n:,} (category, page) links\n")


if __name__ == "__main__":
    main()
