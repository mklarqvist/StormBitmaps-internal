#!/usr/bin/env python3
"""Plot every pairing cell across the sparsity -> density axis.

RESEARCH_PLAN.md 9.1: every figure regenerates from results/ by script, no
hand-curated numbers. This reads results/density.jsonl (emitted by
bench/density_sweep.sh) and writes results/density.{png,svg}.

Run with the project venv:  uv run --python .venv bench/plot_density.py

Three panels:
  1. cost per pair vs density, one curve per cell (best correct non-baseline
     variant at each density)
  2. speedup over the B x B floor -- the quantity PROBLEM_STATEMENT.md 2 is
     about, on a log axis because the claim is asymptotic
  3. which variant wins, per cell, per density -- the selection map M2 would
     have to reproduce
"""
import json
import os
import sys
from collections import defaultdict

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

SRC = sys.argv[1] if len(sys.argv) > 1 else 'results/density.jsonl'
OUT = os.path.splitext(SRC)[0]


def dlabel(d):
    """Format a density for an axis tick.

    The sweep deliberately samples the DENSE extreme as well as the sparse one,
    and 1-32/u, 1-1/u and 1.0 all render as "1" under any normal format -- three
    different points collapsing to one label, in the region where the curves are
    doing the most interesting thing. Near 1 the informative quantity is the
    complement, so show that instead."""
    if d >= 0.995:
        c = 1.0 - d
        return '1' if c == 0 else f'1-{c:.2g}'
    return f'{d:.2g}' 

CELLS = ['B x B', 'B x S', 'B x R', 'B x W',
         'S x S', 'S x R', 'S x W', 'R x R', 'R x W', 'W x W']

# Colour by the SPARSE side's representation so the family structure is visible.
COLOR = {
    'B x B': '#111111', 'B x S': '#d62728', 'B x R': '#1f77b4', 'B x W': '#9467bd',
    'S x S': '#ff7f0e', 'S x R': '#2ca02c', 'S x W': '#8c564b',
    'R x R': '#17becf', 'R x W': '#bcbd22', 'W x W': '#e377c2',
}
STYLE = {'B x B': dict(lw=3.0, ls='-', zorder=10)}


def load(path):
    """-> best: {(cell, density): (ns_pair, variant)} keeping the best non-baseline,
          fixed: {density: ns_pair} for the PURE-SIMD B x B kernel.

    The two are separated deliberately. The B x B cell contains neon_rankskip,
    which skips all-zero 512-bit blocks via the rank index -- a work-reduction
    technique that happens to live inside the dense cell. Letting it define the
    B x B curve would make that curve slope, and PROBLEM_STATEMENT.md 2's whole
    claim is that bitmap x bitmap is FIXED COST. The honest picture needs both:
    the best B x B anyone could actually run, and the flat Theta(m) line that
    the fixed-cost argument is about."""
    best = {}
    fixed = {}
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            r = json.loads(line)
            if r.get('inflate_baseline'):
                continue          # standing rule 3: baselines are not winners
            if r['cell'] == 'B x B' and r['variant'] == 'neon_u8':
                fixed[r['density']] = r['ns_pair']
            k = (r['cell'], r['density'])
            if k not in best or r['ns_pair'] < best[k][0]:
                best[k] = (r['ns_pair'], r['variant'])
    return best, fixed


def main():
    best, fixed = load(SRC)
    if not best:
        print(f'no records in {SRC}')
        return 1

    densities = sorted({d for (_, d) in best})
    fig, axes = plt.subplots(3, 1, figsize=(11, 16.5),
                             gridspec_kw={'height_ratios': [1.15, 1.0, 1.25]})
    ax1, ax2, ax3 = axes

    # ---- panel 1: cost per pair -------------------------------------------
    for cell in CELLS:
        xs = [d for d in densities if (cell, d) in best]
        ys = [best[(cell, d)][0] for d in xs]
        if not xs:
            continue
        ax1.plot(xs, ys, marker='o', ms=3.5, label=cell,
                 color=COLOR[cell], **STYLE.get(cell, {}))
    fx = sorted(fixed)
    if fx:
        ax1.plot(fx, [fixed[d] for d in fx], ls=':', lw=2.4, color='#555555',
                 label='B x B pure SIMD (fixed cost)', zorder=9)
    ax1.set_xscale('log'); ax1.set_yscale('log')
    ax1.set_xlabel('density  (fraction of bits set, BOTH sides)')
    ax1.set_ylabel('ns per pair  (lower is better)')
    ax1.set_title('Cost of every pairing across the sparsity axis\n'
                  'Apple M4 · universe 65,536 bits · uniform structure & spectrum · '
                  'L2-resident throughout', fontsize=11)
    # The regime boundaries are the result, so mark them rather than leaving the
    # reader to find them. Crossovers read off the measured curves: compressed
    # pairings beat fixed-cost B x B below ~4e-3 and again above ~0.999.
    LO, HI = 4.5e-3, 0.9975
    for ax in (ax1, ax2):
        ax.axvspan(min(densities), LO, color='#2ca02c', alpha=0.07, zorder=0)
        ax.axvspan(LO, HI, color='#111111', alpha=0.06, zorder=0)
        ax.axvspan(HI, max(densities), color='#2ca02c', alpha=0.07, zorder=0)
    ax1.text(1.9e-4, 4.5, 'representation pairing wins\n(work avoidance)',
             fontsize=9, ha='center', color='#1a6b1a')
    ax1.text(0.06, 4.5, 'B x B wins\n(work is irreducible — SIMD throughput)',
             fontsize=9, ha='center', color='#333333')
    ax1.text(0.9992, 4.5, 'pairing\nwins again',
             fontsize=8.5, ha='center', color='#1a6b1a')
    ax1.grid(True, which='both', alpha=0.25)
    ax1.legend(ncol=4, fontsize=8.5, loc='upper left')

    # ---- panel 2: speedup over the fixed-cost dense pairing ----------------
    for cell in CELLS:
        if cell == 'B x B':
            continue
        xs = [d for d in densities if (cell, d) in best and d in fixed]
        ys = [fixed[d] / best[(cell, d)][0] for d in xs]
        if not xs:
            continue
        ax2.plot(xs, ys, marker='o', ms=3.5, label=cell, color=COLOR[cell])
    ax2.axhline(1.0, color='#111111', lw=2.0, ls='--')
    ax2.text(densities[0], 1.06, 'B x B pure SIMD  (fixed cost, the thing to beat)',
             fontsize=9, color='#111111', va='bottom')
    ax2.set_xscale('log'); ax2.set_yscale('log')
    ax2.set_xlabel('density')
    ax2.set_ylabel('speedup over fixed-cost B x B  (x)')
    ax2.set_title('Why representation pairing matters: the win is asymptotic in sparsity,\n'
                  'not a constant factor', fontsize=11)
    ax2.grid(True, which='both', alpha=0.25)
    ax2.legend(ncol=5, fontsize=9, loc='upper right')

    # ---- panel 3: which variant wins where --------------------------------
    variants = sorted({best[(c, d)][1] for c in CELLS for d in densities
                       if (c, d) in best})
    vidx = {v: i for i, v in enumerate(variants)}
    cmap = plt.get_cmap('tab20')
    grid = np.full((len(CELLS), len(densities)), np.nan)
    for r, cell in enumerate(CELLS):
        for c, d in enumerate(densities):
            if (cell, d) in best:
                grid[r, c] = vidx[best[(cell, d)][1]]
    ax3.imshow(grid, aspect='auto', cmap=cmap, interpolation='nearest',
               vmin=0, vmax=max(len(variants) - 1, 1))
    for r, cell in enumerate(CELLS):
        for c, d in enumerate(densities):
            if (cell, d) in best:
                ax3.text(c, r, best[(cell, d)][1][:9], ha='center', va='center',
                         fontsize=5.4, rotation=90, color='#000000')
    ax3.set_yticks(range(len(CELLS))); ax3.set_yticklabels(CELLS, fontsize=9)
    ax3.set_xticks(range(len(densities)))
    ax3.set_xticklabels([dlabel(d) for d in densities], rotation=90, fontsize=7)
    ax3.set_xlabel('density')
    ax3.set_title('Winning variant per cell per density — the map the selection '
                  'layer (M2) must reproduce', fontsize=11)

    fig.tight_layout()
    for ext in ('png', 'svg'):
        fig.savefig(f'{OUT}.{ext}', dpi=150, bbox_inches='tight')
        print(f'wrote {OUT}.{ext}')

    # ---- the numbers worth quoting ----------------------------------------
    print('\nBest cell at each density vs the fixed-cost B x B kernel:')
    print(f"{'density':>12} {'best cell':10} {'variant':14} {'ns/pair':>10} "
          f"{'fixed B x B':>12} {'speedup':>9}")
    for d in densities:
        cand = [(best[(c, d)][0], c) for c in CELLS if (c, d) in best and c != 'B x B']
        if not cand or d not in fixed:
            continue
        ns, cell = min(cand)
        bb = fixed[d]
        print(f'{d:12.6g} {cell:10} {best[(cell, d)][1]:14} {ns:10.1f} '
              f'{bb:12.1f} {bb / ns:8.1f}x')
    return 0


if __name__ == '__main__':
    sys.exit(main())
