#!/usr/bin/env python3
"""Extract per-cell winners from bench/sweep.sh output, and diff two runs.

The iteration loop needs one question answered cheaply and repeatedly: for each
(corpus, cell), what is the best CORRECT non-baseline variant and how fast is it?
Reading that out of the raw tables by eye does not scale past a couple of rounds
and invites picking the number that flatters the change.

Baseline-flagged variants (the inflate-to-bitmap fallbacks, AGENTS.md standing
rule 3) are excluded from "best" -- they exist to be beaten, not to win.

Usage:
    bench/compare.py results/iter1_raw.txt
    bench/compare.py results/baseline_raw.txt results/iter1_raw.txt
"""
import re
import sys

CORPUS_RE = re.compile(r'^\s{2}([A-Z]\d)\s+(.*)$')
# The cell header gained a work-unit suffix when the harness started reporting
# ns/unit; both forms are accepted so older result files stay comparable.
CELL_RE   = re.compile(r'^  ([BSRW] x [BSRW])\s*(?:\(work unit:.*)?$')
# Row layouts, old and new:
#   name  ns/pair  relx  flags  note
#   name  ns/pair  relx  ns/unit  cyc/unit  flags  note
ROW_NEW   = re.compile(r'^  ([a-z_0-9]+)\s+([0-9.]+)\s+([0-9.]+)x\s+([0-9.]+)\s+([0-9.]+)\s+(\S*)\s*(.*)$')
ROW_OLD   = re.compile(r'^  ([a-z_0-9]+)\s+([0-9.]+)\s+([0-9.]+)x\s+(\S*)\s*(.*)$')
WRONG_RE  = re.compile(r'^  ([a-z_0-9]+)\s+-\s+WRONG')


def parse(path):
    """-> {corpus: {cell: [(variant, ns, flags)]}}, plus a set of wrong variants."""
    out, wrong = {}, set()
    corpus = cell = None
    for line in open(path):
        line = line.rstrip('\n')
        m = CORPUS_RE.match(line)
        if m and re.match(r'^[A-Z]\d$', m.group(1)):
            corpus = m.group(1)
            out.setdefault(corpus, {})
            continue
        m = CELL_RE.match(line)
        if m:
            cell = m.group(1)
            if corpus:
                out[corpus].setdefault(cell, [])
            continue
        m = WRONG_RE.match(line)
        if m and corpus and cell:
            wrong.add((corpus, cell, m.group(1)))
            continue
        m = ROW_NEW.match(line)
        if m and corpus and cell:
            out[corpus][cell].append((m.group(1), float(m.group(2)), m.group(6)))
            continue
        m = ROW_OLD.match(line)
        if m and corpus and cell:
            out[corpus][cell].append((m.group(1), float(m.group(2)), m.group(4)))
    return out, wrong


def best(rows):
    """Best variant excluding the labelled inflate-to-bitmap baselines."""
    cand = [r for r in rows if 'I' not in r[2]]
    return min(cand, key=lambda r: r[1]) if cand else None


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    new, wrong = parse(sys.argv[-1])
    old = parse(sys.argv[1])[0] if len(sys.argv) > 2 else None

    if wrong:
        print("INCORRECT VARIANTS (excluded):")
        for c, cell, v in sorted(wrong):
            print(f"    {c} {cell} {v}")
        print()

    hdr = f"{'corpus':7} {'cell':8} {'best variant':16} {'ns/pair':>10}"
    if old:
        hdr += f" {'was':>10} {'change':>9}"
    print(hdr)
    print('-' * len(hdr))

    improved = regressed = 0
    for corpus in sorted(new):
        for cell in sorted(new[corpus]):
            b = best(new[corpus][cell])
            if not b:
                continue
            line = f"{corpus:7} {cell:8} {b[0]:16} {b[1]:10.2f}"
            if old:
                ob = best(old.get(corpus, {}).get(cell, []))
                if ob:
                    ratio = ob[1] / b[1]
                    tag = ''
                    if ratio > 1.02:
                        tag, improved = '  FASTER', improved + 1
                    elif ratio < 0.98:
                        tag, regressed = '  SLOWER', regressed + 1
                    line += f" {ob[1]:10.2f} {ratio:8.2f}x{tag}"
            print(line)
    if old:
        print(f"\n{improved} cell-corpus points improved, {regressed} regressed")
    return 0


if __name__ == '__main__':
    sys.exit(main())
