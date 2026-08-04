#!/usr/bin/env python3
"""Figures F4, F5, F6 for the paper (RESEARCH_PLAN.md §14.3).

All three plot data that was already measured and recorded; no new benchmark
runs. F5 parses results/hosts/*_dsweep.txt directly. F4 and F6 come from the
measurement tables recorded in results/OPTLOG.md (F11 and the P4 sweep) — they
are transcribed here rather than re-parsed because those runs used bespoke
scripts whose raw output was summarised, not retained per-point.

Run: uv run --python .venv bench/plot_paper.py
"""
import glob
import os
import re

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

OUT = 'results'

# --- F4: the two strategies scale in opposite directions (OPTLOG F11) --------
# B x B, Apple M4, clustered/1-over-i, 96 rows. cycles/word.
F4 = [
    # (corpus MB, label, scalar, best SIMD, zone map)
    (1,   'L2',   0.511, 0.336, 0.058),
    (12,  'SLC',  0.562, 0.437, 0.038),
    (48,  'DRAM', 0.796, 0.679, 0.039),
]

# --- F6: run LENGTH drops out of B x R's cost (bench/p4_runlength.sh) -------
# Run COUNT pinned at ~15.3/pair, universe 1,048,576, Apple M4. ns per run.
F6 = [
    (64,    1.72, 2.20),
    (256,   2.91, 2.51),
    (1024,  4.80, 2.74),
    (4096, 10.46, 4.49),
    (16384,31.75, 3.29),
]

HOSTS = {'apple-m4': 'Apple M4 (NEON)',
         'fpga-neo1': 'Neoverse (SVE)',
         'fpga-neo2': 'Neoverse (SVE2)',
         'fpga-sapphire': 'Sapphire Rapids (AVX-512)'}


def parse_f5():
    """-> {host: [(density, croaring_ns, best_ns)]} from the dsweep outputs."""
    out = {}
    for path in sorted(glob.glob('results/hosts/*_dsweep.txt')):
        host = os.path.basename(path).replace('_dsweep.txt', '')
        cur, rec = None, {}
        for line in open(path):
            m = re.match(r'# host=\S+ corpus=(\S+) d=(\S+) ', line)
            if m:
                cur = (m.group(1), float(m.group(2)))
                continue
            m = re.match(r'CRoaring run_optimize\s+([0-9.]+)', line)
            if m and cur:
                rec.setdefault(cur, {})['ro'] = float(m.group(1))
            m = re.match(r'BEST storm cell: \S+ x \S+ at ([0-9.]+)', line)
            if m and cur:
                rec.setdefault(cur, {})['best'] = float(m.group(1))
        pts = [(k[1], v['ro'], v['best'])
               for k, v in rec.items()
               if k[0].startswith('clustered') and 'best' in v and 'ro' in v]
        if pts:
            out[host] = sorted(pts)
    return out


fig, ax = plt.subplots(1, 3, figsize=(16.5, 4.9))

# ---- F4 --------------------------------------------------------------------
xs = [r[0] for r in F4]
ax[0].plot(xs, [r[2] / r[3] for r in F4], 'o-', lw=2.2, color='#1f77b4',
           label='SIMD vs scalar')
ax[0].plot(xs, [r[2] / r[4] for r in F4], 's-', lw=2.2, color='#d62728',
           label='zone map vs scalar')
for x, lab, *_ in F4:
    ax[0].annotate(lab, (x, 1.05), ha='center', fontsize=9, color='#555')
ax[0].axhline(1.0, color='#111', lw=1, ls='--')
ax[0].set_xscale('log'); ax[0].set_yscale('log')
ax[0].set_xlabel('corpus footprint (MB)')
ax[0].set_ylabel('speedup over scalar (x)')
ax[0].set_title('F4  The two strategies scale in\nOPPOSITE directions', fontsize=11)
ax[0].grid(True, which='both', alpha=0.25)
ax[0].legend(fontsize=9)

# ---- F5 --------------------------------------------------------------------
for host, pts in parse_f5().items():
    ax[1].plot([p[0] for p in pts], [p[1] / p[2] for p in pts], 'o-', ms=4,
               label=HOSTS.get(host, host))
ax[1].axhline(1.0, color='#111', lw=1.5, ls='--')
ax[1].text(2e-4, 1.06, 'CRoaring (run_optimize)', fontsize=8.5, color='#111')
ax[1].set_xscale('log'); ax[1].set_yscale('log')
ax[1].set_xlabel('density (clustered, 1/i spectrum)')
ax[1].set_ylabel('speedup over CRoaring (x)')
ax[1].set_title('F5  vs a run_optimize-tuned CRoaring,\nfour microarchitectures', fontsize=11)
ax[1].grid(True, which='both', alpha=0.25)
ax[1].legend(fontsize=8)

# ---- F6 --------------------------------------------------------------------
rl = [r[0] for r in F6]
ax[2].plot(rl, [r[1] for r in F6], 'o-', lw=2.2, color='#1f77b4', label='no index')
ax[2].plot(rl, [r[2] for r in F6], 's-', lw=2.2, color='#d62728', label='rank9 index')
ax[2].set_xscale('log'); ax[2].set_yscale('log')
ax[2].set_xlabel('mean run LENGTH (bits) — run COUNT pinned at ~15.3/pair')
ax[2].set_ylabel('ns per run')
ax[2].set_title('F6  Run length drops out of\nB x R cost (claim P4)', fontsize=11)
ax[2].grid(True, which='both', alpha=0.25)
ax[2].legend(fontsize=9)

fig.tight_layout()
for ext in ('png', 'svg'):
    fig.savefig(f'{OUT}/paper_figures.{ext}', dpi=150, bbox_inches='tight')
    print(f'wrote {OUT}/paper_figures.{ext}')

print('\nF4  SIMD %.2fx -> %.2fx   zone map %.1fx -> %.1fx  (L2 -> DRAM)'
      % (F4[0][2] / F4[0][3], F4[-1][2] / F4[-1][3],
         F4[0][2] / F4[0][4], F4[-1][2] / F4[-1][4]))
print('F6  no-index grows %.1fx across the sweep; indexed shows no trend'
      % (F6[-1][1] / F6[0][1]))
for host, pts in parse_f5().items():
    best = max(p[1] / p[2] for p in pts)
    print(f'F5  {HOSTS.get(host, host):32} peak {best:5.1f}x over CRoaring')
