#!/usr/bin/env python3
"""Plot the similarity-threshold sweep (RESEARCH_PLAN.md §15.23, C24).

    .venv/bin/python bench/plot_threshold.py [results/threshold/sweep.csv]

Thresholded mode is an opt-in contract, so its benefit is a *function of a
parameter the caller sets*, not one number. These panels are meant to be read at
whatever cutoff a given application actually uses — hence a curve per corpus
rather than a bar chart at some chosen t.

Panel 1 speedup vs t, gated (what a user actually gets)
Panel 2 candidates generated per pair — the mechanism behind panel 1
Panel 3 hit rate: what fraction of pairs clear the threshold
Panel 4 speedup vs mean row length at fixed t, showing where the gate flips
"""
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

CSV = Path(sys.argv[1] if len(sys.argv) > 1 else "results/threshold/sweep.csv")
OUT = CSV.with_suffix(".png")

rows = {}
with CSV.open() as f:
    hdr = f.readline().strip().split(",")
    for ln in f:
        p = ln.strip().split(",")
        if len(p) != len(hdr):
            continue
        d = dict(zip(hdr, p))
        rows.setdefault(d["corpus"], []).append(d)

if not rows:
    sys.exit(f"no data in {CSV}")

# Any mismatch invalidates the corresponding curve outright — a wrong result is
# not a slow result, and plotting it beside correct ones would imply otherwise.
bad = {c for c, v in rows.items() if any(x["correct"] != "1" for x in v)}
if bad:
    print(f"WARNING: hit-set mismatch in {sorted(bad)} — excluded")
    rows = {c: v for c, v in rows.items() if c not in bad}

for c in rows:
    rows[c].sort(key=lambda d: float(d["t"]))

# Order by speedup at the conservative end so the legend and the colour ramp
# both read best-to-worst, which is how the panels are meant to be scanned.
order = sorted(rows, key=lambda c: -float(rows[c][0]["gated_speedup"]))
cmap = plt.get_cmap("viridis")
colors = {c: cmap(i / max(1, len(order) - 1)) for i, c in enumerate(order)}

fig, ax = plt.subplots(2, 2, figsize=(14, 10))
fig.suptitle("Thresholded mode: speedup as a function of the caller's similarity cutoff\n"
             "exact all-pairs remains the default; every point verified to produce "
             "an identical hit set", fontsize=12)

# --- 1. gated speedup -------------------------------------------------------
a = ax[0][0]
for c in order:
    t = [float(d["t"]) for d in rows[c]]
    s = [float(d["gated_speedup"]) for d in rows[c]]
    a.plot(t, s, label=c, color=colors[c], lw=1.6)
a.axhline(1.0, color="k", ls="--", lw=1, zorder=0)
a.set_yscale("log"); a.set_xlabel("Jaccard threshold t"); a.set_ylabel("speedup vs exact all-pairs")
a.set_title("Gated speedup (what the user gets)")
a.grid(alpha=.3, which="both")
a.legend(fontsize=7, ncol=2)
a.text(0.02, 0.97, "1.0 = gate fell back to the exact scan", transform=a.transAxes,
       va="top", fontsize=7, style="italic")

# --- 2. candidates per pair -------------------------------------------------
a = ax[0][1]
for c in order:
    t = [float(d["t"]) for d in rows[c]]
    v = [float(d["candidates"]) / max(1.0, float(d["pairs"])) for d in rows[c]]
    a.plot(t, v, label=c, color=colors[c], lw=1.6)
a.set_yscale("log"); a.set_xlabel("Jaccard threshold t")
a.set_ylabel("candidates generated / pair")
a.set_title("Mechanism: prefix filtering never enumerates all pairs")
a.grid(alpha=.3, which="both")

# --- 3. hit rate ------------------------------------------------------------
a = ax[1][0]
for c in order:
    t = [float(d["t"]) for d in rows[c]]
    v = [100.0 * float(d["hits"]) / max(1.0, float(d["pairs"])) for d in rows[c]]
    a.plot(t, v, label=c, color=colors[c], lw=1.6)
a.set_yscale("log"); a.set_xlabel("Jaccard threshold t")
a.set_ylabel("% of pairs above threshold")
a.set_title("How much of the answer the caller actually wants")
a.grid(alpha=.3, which="both")

# --- 4. ungated vs gated at the conservative end ----------------------------
a = ax[1][1]
tt = min(float(d["t"]) for d in rows[order[0]])
names, un, ga = [], [], []
for c in order:
    d = min(rows[c], key=lambda d: abs(float(d["t"]) - tt))
    names.append(c); un.append(float(d["speedup"])); ga.append(float(d["gated_speedup"]))
x = np.arange(len(names))
a.bar(x - .2, un, .4, label="ungated", color="#c0504d")
a.bar(x + .2, ga, .4, label="gated", color="#4f81bd")
a.axhline(1.0, color="k", ls="--", lw=1)
a.set_yscale("log"); a.set_xticks(x)
a.set_xticklabels(names, rotation=45, ha="right", fontsize=7)
a.set_ylabel("speedup"); a.set_title(f"Gate turns losses into par (t={tt:g}, the conservative end)")
a.legend(fontsize=8); a.grid(alpha=.3, axis="y", which="both")

fig.tight_layout(rect=[0, 0, 1, 0.94])
fig.savefig(OUT, dpi=150)
print(f"wrote {OUT}")

# Console summary at the conservative end, which is what should be quoted.
print(f"\nat t={tt:g}:")
for c in order:
    d = min(rows[c], key=lambda d: abs(float(d["t"]) - tt))
    print(f"  {c:<24} gated {float(d['gated_speedup']):8.2f}x  "
          f"({d['gate']}, hits {100*float(d['hits'])/float(d['pairs']):.4f}%)")
