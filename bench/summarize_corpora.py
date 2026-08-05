#!/usr/bin/env python3
"""Collate bench/run_corpora.sh output into one markdown table.

Ordered by density, because density is the paper's independent variable
(RESEARCH_PLAN.md 14): the deliverable is a map of which representation pairing
wins where, not a single headline ratio. Reading the table top to bottom should
show the winning cell migrating from the sparse cells to B x B as density rises.
"""
import re
import sys
from pathlib import Path

CELLS = ["B x B zonemap", "B x S", "B x R", "B x W",
         "S x S", "S x R", "R x R", "W x W"]


def parse(p):
    t = p.read_text()
    h = re.search(r"# host=(\S+).*?d=(\S+) rows=(\d+) universe=(\d+) pairs=(\d+) "
                  r"card_mean=(\S+) runs_mean=(\S+)", t)
    if not h:
        return None
    r = {"name": h.group(1), "d": float(h.group(2)), "rows": int(h.group(3)),
         "m": int(h.group(4)), "pairs": int(h.group(5)),
         "card": float(h.group(6)), "runs": float(h.group(7))}
    for label in ["CRoaring", "CRoaring run_optimize", "storm B x B dense"] + \
                 [f"storm {c}" for c in CELLS]:
        mm = re.search(r"^" + re.escape(label) + r"\s+([\d.]+)\s", t, re.M)
        if mm:
            r[label] = float(mm.group(1))
    b = re.search(r"BEST storm cell: (.+?) at ([\d.]+) ns/pair", t)
    if b:
        r["best_cell"], r["best_ns"] = b.group(1).strip(), float(b.group(2))
    return r


def main():
    d = Path(sys.argv[1] if len(sys.argv) > 1 else "results/corpora")
    rows = [x for x in (parse(p) for p in sorted(d.glob("*.txt"))
                        if ".triage" not in p.name and p.name != "RUN.log") if x]
    rows.sort(key=lambda r: r["d"])

    print("| corpus | universe m | sets | mean \\|Xi\\| | density | best cell | "
          "ns/pair | vs CRoaring_ro | vs all-bitmap |")
    print("|---|---:|---:|---:|---:|---|---:|---:|---:|")
    for r in rows:
        ro = r.get("CRoaring run_optimize")
        bb = r.get("storm B x B dense")
        bn = r.get("best_ns")
        if not (ro and bn):
            continue
        print(f"| {r['name']} | {r['m']:,} | {r['rows']} | {r['card']:,.0f} | "
              f"{r['d']:.2e} | {r.get('best_cell','?')} | {bn:.1f} | "
              f"**{ro/bn:.2f}x** | {bb/bn:,.0f}x |")

    # Per-cell matrix: shows that no single cell wins everywhere, which is the
    # actual thesis. A table with one dominant column would refute the paper.
    print("\n\n### Per-cell speedup over CRoaring(run_optimize)\n")
    print("| corpus | density | " + " | ".join(CELLS) + " |")
    print("|---|---:|" + "---:|" * len(CELLS))
    for r in rows:
        ro = r.get("CRoaring run_optimize")
        if not ro:
            continue
        cells = []
        best = max((ro / r[f"storm {c}"] for c in CELLS if f"storm {c}" in r),
                   default=0)
        for c in CELLS:
            v = r.get(f"storm {c}")
            if v is None:
                cells.append("--")
            else:
                s = ro / v
                cells.append(f"**{s:.2f}**" if abs(s - best) < 1e-9 else f"{s:.2f}")
        print(f"| {r['name']} | {r['d']:.1e} | " + " | ".join(cells) + " |")

    won = sum(1 for r in rows
              if r.get("CRoaring run_optimize") and r.get("best_ns")
              and r["CRoaring run_optimize"] / r["best_ns"] > 1.0)
    print(f"\n\n{len(rows)} corpora; Storm's best cell beats tuned CRoaring on "
          f"{won}/{len(rows)}.")
    winners = {}
    for r in rows:
        winners[r.get("best_cell", "?")] = winners.get(r.get("best_cell", "?"), 0) + 1
    print("Winning cell distribution: "
          + ", ".join(f"{k} x{v}" for k, v in sorted(winners.items(),
                                                     key=lambda kv: -kv[1])))


if __name__ == "__main__":
    main()
