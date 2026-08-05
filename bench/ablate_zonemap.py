#!/usr/bin/env python3
"""Collate the zone-map ablation emitted by bench_baseline.

The zone map is built unconditionally by build_row(), so "is it worth it?" is
not answerable from the main table -- that table only ever shows the planned
kernel. This pulls the index-free vs index-using pair for each cell, plus the
system-level number that actually matters: best cell when side structures are
allowed, against best cell when they are not.
"""
import glob, re, sys

def g(t, pat):
    m = re.search(pat, t, re.M)
    return float(m.group(1)) if m else None

rows = []
for p in sorted(glob.glob((sys.argv[1] if len(sys.argv) > 1 else 'results/corpora') + '/*.txt')):
    if '.triage' in p:
        continue
    t = open(p).read()
    h = re.search(r"# host=(\S+).*?d=(\S+)", t)
    if not h:
        continue
    rows.append(dict(
        name=h.group(1), d=float(h.group(2)),
        bbf=g(t, r"^B x B  neon_u8 -> occ_sel\s+([\d.]+)"),
        bbo=g(t, r"^B x B  neon_u8 -> occ_sel\s+[\d.]+\s+([\d.]+)"),
        bsf=g(t, r"^B x S  ilp8    -> occ\s+([\d.]+)"),
        bso=g(t, r"^B x S  ilp8    -> occ\s+[\d.]+\s+([\d.]+)"),
        brf=g(t, r"^B x R  neon    -> occ\s+([\d.]+)"),
        bro=g(t, r"^B x R  neon    -> occ\s+[\d.]+\s+([\d.]+)"),
        free=g(t, r"^BEST index-free cell:\s+([\d.]+)"),
        best=g(t, r"^BEST overall \(indexed\):\s+([\d.]+)"),
        buys=g(t, r"^  zone map/rank buys:\s+([\d.]+)"),
        fp=g(t, r"^  zone map footprint:\s+([\d.]+)"),
        cell=(lambda m: m.group(1).strip() if m else '?')(
            re.search(r"BEST storm cell: (.+?) at", t)),
    ))
rows.sort(key=lambda r: r['d'])

rat = lambda a, b: f"{a/b:.2f}x" if a and b else "--"
print("| corpus | density | winning cell | B×B alone | B×S alone | B×R alone | "
      "best no-index | best indexed | **net gain** |")
print("|---|---:|---|---:|---:|---:|---:|---:|---:|")
for r in rows:
    print(f"| {r['name']} | {r['d']:.1e} | {r['cell']} | {rat(r['bbf'],r['bbo'])} | "
          f"{rat(r['bsf'],r['bso'])} | {rat(r['brf'],r['bro'])} | {r['free']:.1f} | "
          f"{r['best']:.1f} | **{r['buys']:.2f}x** |")

helped = sum(1 for r in rows if r['buys'] and r['buys'] > 1.05)
neutral = sum(1 for r in rows if r['buys'] and r['buys'] <= 1.05)
hurt = sum(1 for r in rows if r['buys'] and r['buys'] < 0.95)
print(f"\nNet: helps {helped}/{len(rows)}, neutral {neutral}/{len(rows)}, "
      f"hurts {hurt}/{len(rows)}. Footprint {rows[0]['fp']:.3f}% of bitmap bytes.")
