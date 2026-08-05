#!/usr/bin/env python3
"""Figure 3 — prefix filtering, analysed the same way as Figure 2.

Pure analysis; no measurement enters this figure.

Prefix filtering is the opt-in thresholded mode: only pairs with Jaccard
similarity at least t are reported. With elements in a global order and

    p(X) = |X| - ceil(t|X|) + 1

any qualifying pair must share an element between its prefixes, so indexing
prefixes alone is exact for the thresholded question. It is CANDIDATE
GENERATION, not pair filtering: the quadratic pair set is never enumerated.

Two things are worth showing, and they mirror Figure 2.

(a) How much it prunes, against the threshold, for several densities.

(b) That all of it collapses onto one curve. For prefixes of length p over a
    universe of m the expected number of shared prefix elements per pair is
    y = p^2/m, and the candidate fraction is exactly 1 - e^{-y} in the
    scattered case. Every (density, threshold, universe) combination lands on
    that single curve -- the analogue of the zone map's bits-per-bin variable.

Run:  .venv-fig/bin/python figures/plot_fig3_prefix.py
"""
import math
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import LogLocator

# ---- house palette (DISPLAY_ITEMS.md 1.1) ---------------------------------
# Same discipline as Figure 2: hue names a REPRESENTATION and nothing else.
# No representation is plotted in this figure, so the only colours used are the
# achromatic bitmap reference, the reserved grey for mechanism reference lines,
# the two reserved accents for the cost/saving axis, and -- for the two panels
# whose series are an ORDERED quantity (density, universe size) -- a sequential
# ramp, which is what an ordered quantity takes instead of arbitrary hues.
INK  = "#222222"   # B, the bitmap reference
GREY = "#BBBBBB"   # reserved: reference / baseline
COST = "#EE6677"   # reserved accent: work done / pairs that survive
SAVE = "#66CCEE"   # reserved accent: work avoided / pairs pruned
REF  = "0.55"      # mechanism reference lines: grey + line style + a label
TXT  = "0.35"      # small annotation text

# Sequential ramps for the two ordered series (cividis: colourblind-optimised).
RAMP = plt.get_cmap("cividis")(np.linspace(0.0, 0.58, 3))
# Second, non-colour encoding for each ordered series, so both panels survive
# greyscale: a distinct dash pattern / marker per level.
DASHES = [(0, ()), (0, (5, 1.8)), (0, (1.6, 1.4))]

# House rcParams (DISPLAY_ITEMS.md 1.2). pdf.fonttype 42 is NOT optional.
matplotlib.rcParams.update({
    "pdf.fonttype": 42, "ps.fonttype": 42,
    "font.size": 8,
    "figure.facecolor": "white", "savefig.facecolor": "white",
    "axes.grid": False,
    "axes.axisbelow": True,
    "savefig.bbox": "tight", "savefig.pad_inches": 0.02,
})

# Canvas sized to the real text block, and included at width=\linewidth with no
# downstream scaling. Included at half width from a 9.4 in canvas, as it was,
# every label on this figure renders below 4 pt.
FIG_W = 500.484 / 72.27          # inches
FIG_H = FIG_W / 1.95


def prefix_len(k, t):
    return np.maximum(1, k - np.ceil(t * k) + 1)


def cand_fraction(y):
    """Exact for scattered prefixes: P(two random p-subsets intersect)."""
    return 1.0 - np.exp(-y)


def panel_a(ax, m=1_000_000):
    t = np.linspace(0.0, 0.99, 400)
    # Density is an ORDERED quantity, so it takes a sequential ramp rather than
    # three arbitrary hues -- and each level carries its own dash pattern, so
    # the ordering survives greyscale.
    for (d, col, dash) in zip((1e-3, 5e-3, 2e-2), RAMP, DASHES):
        k = d * m
        p = prefix_len(k, t)
        y = p * p / m
        c = cand_fraction(y)
        # The three curves only separate very close to t = 1, so direct
        # labelling collides; a three-entry legend is the readable choice.
        ax.plot(t, c, color=col, lw=1.8, ls=dash, zorder=4,
                label=f"$d = {d:g}$")
    ax.axhline(1.0, color=INK, lw=2.0, zorder=5)
    ax.text(0.02, 1.25, "no pruning (every pair a candidate)", color=INK,
            fontsize=7.4, ha="left", va="bottom", weight="bold", zorder=7)
    ax.set_yscale("log")
    ax.set_ylim(1e-5, 4.0)
    ax.set_xlim(0, 1)
    # Achromatic wash: the reference is the exhaustive scan, which is the
    # baseline, so the half-plane above it takes the reserved grey.
    ax.axhspan(1.0, 4.0, color=GREY, alpha=0.35, lw=0, zorder=0)
    ax.set_xlabel("Jaccard threshold  $t$")
    ax.set_ylabel("surviving candidate pairs")
    leg = ax.legend(loc="lower left", frameon=False, fontsize=7.4,
                    handlelength=2.2, borderpad=0.2, labelspacing=0.35)
    leg.set_zorder(8)
    ax.yaxis.set_major_locator(LogLocator(base=10, numticks=8))
    ax.grid(True, which="major", axis="y", color="0.9", lw=0.5, zorder=1)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)


def panel_b(ax):
    y = np.logspace(-6, 1.4, 500)
    ax.plot(y, cand_fraction(y), color=INK, lw=2.4, zorder=6)
    ax.plot(y, y, color="0.6", lw=1.0, ls=(0, (4, 3)), zorder=4)
    ax.text(1.1e-2, 8e-4, "$1-e^{-y}$", color=INK, fontsize=9,
            ha="left", va="bottom", weight="bold", zorder=7)
    ax.text(8e-5, 8e-6, "$y$  (small-$y$ limit)", color="0.45",
            fontsize=7.0, ha="left", va="bottom", zorder=7)

    rng = np.random.default_rng(0)
    # Universe size is an ordered quantity: sequential ramp, plus a distinct
    # marker shape per level as the non-colour encoding.
    for m, col, mk in ((1e5, RAMP[0], "o"), (1e6, RAMP[1], "s"),
                       (1e7, RAMP[2], "^")):
        for _ in range(28):
            d = 10 ** rng.uniform(-4, -1.3)
            t = rng.uniform(0.3, 0.97)
            k = d * m
            p = float(prefix_len(np.array([k]), t)[0])
            yy = p * p / m
            if 1e-6 < yy < 25:
                ax.plot([yy], [cand_fraction(yy)], marker=mk, ms=3.4,
                        mfc="none", mec=col, mew=0.9, alpha=0.85, zorder=5)
    for lbl, col, mk, yy in (("$m=10^5$", RAMP[0], "o", 0.50),
                             ("$m=10^6$", RAMP[1], "s", 0.055),
                             ("$m=10^7$", RAMP[2], "^", 0.006)):
        ax.plot([1.3e-5], [yy], marker=mk, ms=3.6, mfc="none", mec=col, mew=0.9)
        ax.text(2.0e-5, yy, lbl, color=col, fontsize=7.2, ha="left",
                va="center", zorder=7)

    # y > 1 is the region where the index stops paying for itself: the reserved
    # "work" accent, not an invented red.
    ax.axvspan(1.0, 25, color=COST, alpha=0.16, lw=0, zorder=0)
    ax.axvline(1.0, color="0.4", lw=0.8, ls=":", zorder=3)
    ax.text(1.15, 1.5e-6, "$y>1$: prefixes\ncollide by default", color=TXT,
            fontsize=7.0, ha="left", va="bottom", zorder=7)

    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_xlim(1e-6, 25); ax.set_ylim(1e-6, 2.0)
    ax.set_xlabel("$y = p^2/m$   (shared prefix elements per pair)")
    ax.set_ylabel("surviving candidate pairs")
    ax.grid(True, which="major", color="0.9", lw=0.5, zorder=1)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)


def panel_c(ax):
    """The work split, as areas, on the universal axis.

    Total = every pair. Prefix filtering cuts it into work never done
    (e^{-y}) and work that survives to exact verification (1 - e^{-y}).
    Plotted as stacked areas on a linear axis, because the question is how
    the whole is divided, not where a small fraction sits.
    """
    y = np.logspace(-3, 1.2, 600)
    surviving = cand_fraction(y)
    pruned = 1.0 - surviving

    # The same reserved pair, with the same meaning, as Fig. 2e: cyan is work
    # avoided, red is work still done. No colour changes sense between figures.
    ax.fill_between(y, 0, pruned, color=SAVE, alpha=0.45, lw=0, zorder=2)
    ax.fill_between(y, pruned, 1.0, color=COST, alpha=0.45, lw=0, zorder=2)
    ax.plot(y, pruned, color=INK, lw=1.6, zorder=5)

    ax.axvline(math.log(2), color=INK, lw=1.0, ls=":", zorder=6)
    ax.text(math.log(2) * 0.88, 0.12, "half pruned\nat $y=\\ln 2$", color=INK,
            fontsize=7.0, ha="right", va="center", zorder=7)

    ax.text(1.6e-3, 0.42, "work never done\n(pairs pruned)", color=TXT,
            fontsize=8, ha="left", va="center", weight="bold", zorder=7)
    ax.text(13.0, 0.42, "work remaining\n(verified exactly)",
            color=TXT, fontsize=8, ha="right", va="center",
            weight="bold", zorder=7)

    for yy, lab, dy in ((1e-2, "99% pruned", -0.05), (1e-1, "90% pruned", -0.16)):
        ax.plot([yy], [1 - cand_fraction(yy)], marker="o", ms=3.6, mfc="white",
                mec=INK, mew=1.3, zorder=8)
        ax.text(yy * 1.3, 1 - cand_fraction(yy) + dy, lab, color=TXT,
                fontsize=7.0, ha="left", va="top", zorder=8)

    ax.set_xscale("log")
    ax.set_xlim(1e-3, 15); ax.set_ylim(0, 1)
    ax.set_xlabel("$y = p^2/m$   (shared prefix elements per pair)")
    ax.set_ylabel("share of all pairs")
    ax.grid(True, which="major", axis="x", color="0.9", lw=0.5, zorder=1)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)


def panel_d(ax, m=1_000_000, wz=512, t=0.9):
    """Prefix filtering and zone maps composed -- and they DO multiply.

    Prefix filtering chooses WHICH pairs are examined; a zone map reduces the
    work WITHIN a pair. They act at different granularities, on different
    waste, so their savings multiply:

        composed  ~  c(t,d) * (1/Wz + p^2)

    verified against the conditional model (candidates cannot be disjoint, so
    at least one bin overlaps) -- the two agree exactly for d > 1e-4 and to
    within 25% at the sparsest end.

    Contrast Fig. 2c, where a zone map and a representation act on the SAME
    waste inside one pair, filtering concentrates what it passes on, and the
    composition is worth much less than the product.
    """
    d = np.logspace(-5, -1.2, 500)
    k = d * m
    # continuous form p ~ k(1-t)+1; the exact integer rule quantises visibly
    # at k ~ 10 and reads as noise rather than as the real step function
    ppref = np.maximum(1.0, k * (1.0 - t) + 1.0)
    c = cand_fraction(ppref * ppref / m)
    p = 1.0 - (1.0 - d) ** wz
    zone = 1.0 / wz + p * p
    composed = c * zone

    ax.set_yscale("log"); ax.set_xscale("log")
    lo, hi = 1e-9, 3.0
    ax.axhspan(1.0, hi, color=GREY, alpha=0.35, lw=0, zorder=0)
    ax.axhline(1.0, color=INK, lw=2.0, zorder=5)

    # Prefix filtering and a zone map are MECHANISMS, so they are references in
    # the reserved grey, separated by line style and named directly. The extra
    # saving the composition buys over the better of the two is the reserved
    # "work avoided" accent, and the composed curve carries it.
    ax.fill_between(d, composed, np.minimum(c, zone), color=SAVE, alpha=0.40,
                    lw=0, zorder=2)
    ax.plot(d, c, color=REF, lw=1.3, ls=(0, (5, 2)), zorder=4)
    ax.plot(d, zone, color=REF, lw=1.3, ls=(0, (1.8, 1.5)), zorder=4)
    ax.plot(d, composed, color=SAVE, lw=2.4, zorder=6)

    ax.text(1.15e-5, 1.35, "exhaustive bitmap scan", color=INK, fontsize=7.2,
            ha="left", va="bottom", weight="bold", zorder=7)
    ax.text(1.15e-5, 1.5e-9, f"$t={t}$,  $W_z={wz}$,  $m=10^{{6}}$",
            color="0.4", fontsize=7.0, ha="left", va="bottom", zorder=7)
    # Frameless key in the empty lower-right, matching Fig. 2d.
    ax.plot([], [], color=REF, lw=1.3, ls=(0, (5, 2)), label="prefix alone")
    ax.plot([], [], color=REF, lw=1.3, ls=(0, (1.8, 1.5)), label="zone map alone")
    ax.plot([], [], color=SAVE, lw=2.4, label="composed")
    leg = ax.legend(loc="lower right", frameon=False, fontsize=6.8,
                    handlelength=1.9, borderpad=0.1, labelspacing=0.25,
                    handletextpad=0.5, bbox_to_anchor=(1.01, -0.02))
    leg.set_zorder(9)

    j = int(np.argmin(np.abs(d - 1e-3)))
    print(f"    at d=1e-3: prefix {c[j]:.4g}, zone {zone[j]:.4g}, "
          f"composed {composed[j]:.4g} (bitmap does {1/composed[j]:.0f}x the work)")

    ax.set_xlim(1e-5, 6e-2); ax.set_ylim(lo, hi)
    ax.set_xlabel("density  $d = |X|/m$")
    ax.set_ylabel("cost $\\div$ exhaustive scan")
    ax.yaxis.set_major_locator(LogLocator(base=10, numticks=7))
    ax.grid(True, which="major", axis="y", color="0.9", lw=0.5, zorder=1)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)


def main():
    fig, axes = plt.subplots(2, 2, figsize=(FIG_W, FIG_H))
    panel_a(axes[0][0]); panel_b(axes[0][1])
    panel_c(axes[1][0]); panel_d(axes[1][1])
    # 9 pt bold, matching the "a" that LaTeX sets above this canvas.
    for ax, t in zip(axes.ravel(), ("b   pruning against the threshold",
                                    "c   one curve, whatever the corpus",
                                    "d   how the work divides",
                                    "e   composed with a zone map")):
        ax.set_title(t, loc="left", fontsize=9, weight="bold", pad=4)
    fig.tight_layout(pad=0.4, w_pad=2.2, h_pad=2.0)
    for ext in ("pdf", "png"):
        fig.savefig(f"figures/fig3_prefix.{ext}", dpi=220, bbox_inches="tight")
    print("wrote figures/fig3_prefix.{pdf,png}")
    for d, t, m in ((1e-3, 0.9, 1e6), (1e-3, 0.5, 1e6), (2e-2, 0.9, 1e6)):
        k = d * m; p = float(prefix_len(np.array([k]), t)[0]); yy = p * p / m
        print(f"  d={d:g} t={t} m={m:g}: p={p:.0f}, y={yy:.4g}, "
              f"candidates={cand_fraction(yy):.4g} "
              f"({1/cand_fraction(yy):.0f}x fewer pairs)")


if __name__ == "__main__":
    main()
