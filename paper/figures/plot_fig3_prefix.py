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

INK = "#222222"; BLUE = "#4477AA"; GREEN = "#228833"
PURP = "#AA3377"; GOLD = "#CCBB44"; CYAN = "#66CCEE"


def prefix_len(k, t):
    return np.maximum(1, k - np.ceil(t * k) + 1)


def cand_fraction(y):
    """Exact for scattered prefixes: P(two random p-subsets intersect)."""
    return 1.0 - np.exp(-y)


def panel_a(ax, m=1_000_000):
    t = np.linspace(0.0, 0.99, 400)
    for d, col in ((1e-3, BLUE), (5e-3, GREEN), (2e-2, PURP)):
        k = d * m
        p = prefix_len(k, t)
        y = p * p / m
        c = cand_fraction(y)
        # The three curves only separate very close to t = 1, so direct
        # labelling collides; a three-entry legend is the readable choice.
        ax.plot(t, c, color=col, lw=1.8, zorder=4, label=f"$d = {d:g}$")
    ax.axhline(1.0, color=INK, lw=2.0, zorder=5)
    ax.text(0.02, 1.25, "no pruning (every pair a candidate)", color=INK,
            fontsize=7, ha="left", va="bottom", weight="bold", zorder=7)
    ax.set_yscale("log")
    ax.set_ylim(1e-5, 4.0)
    ax.set_xlim(0, 1)
    ax.axhspan(1e-5, 1.0, color=GREEN, alpha=0.08, lw=0, zorder=0)
    ax.axhspan(1.0, 4.0, color="#CC3311", alpha=0.08, lw=0, zorder=0)
    ax.set_xlabel("Jaccard threshold  $t$")
    ax.set_ylabel("fraction of pairs surviving as candidates")
    leg = ax.legend(loc="lower left", frameon=False, fontsize=7.5,
                    handlelength=1.6, borderpad=0.2, labelspacing=0.35)
    leg.set_zorder(8)
    ax.yaxis.set_major_locator(LogLocator(base=10, numticks=8))
    ax.grid(True, which="major", axis="y", color="0.9", lw=0.5, zorder=1)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)


def panel_b(ax):
    y = np.logspace(-6, 1.4, 500)
    ax.plot(y, cand_fraction(y), color=INK, lw=2.4, zorder=6)
    ax.plot(y, y, color="0.6", lw=1.0, ls=(0, (4, 3)), zorder=4)
    ax.text(2e-4, 2.2e-3, "$1-e^{-y}$", color=INK, fontsize=9,
            ha="left", va="bottom", weight="bold", zorder=7)
    ax.text(3.5e-3, 4.0e-4, "$y$  (small-$y$ limit)", color="0.45",
            fontsize=6.8, ha="left", va="top", zorder=7)

    rng = np.random.default_rng(0)
    for m, col, mk in ((1e5, BLUE, "o"), (1e6, GREEN, "s"), (1e7, PURP, "^")):
        for _ in range(28):
            d = 10 ** rng.uniform(-4, -1.3)
            t = rng.uniform(0.3, 0.97)
            k = d * m
            p = float(prefix_len(np.array([k]), t)[0])
            yy = p * p / m
            if 1e-6 < yy < 25:
                ax.plot([yy], [cand_fraction(yy)], marker=mk, ms=3.4,
                        mfc="none", mec=col, mew=0.9, alpha=0.85, zorder=5)
    for lbl, col, mk, yy in (("$m=10^5$", BLUE, "o", 0.55),
                             ("$m=10^6$", GREEN, "s", 0.28),
                             ("$m=10^7$", PURP, "^", 0.14)):
        ax.plot([1.3e-5], [yy], marker=mk, ms=3.6, mfc="none", mec=col, mew=0.9)
        ax.text(2.0e-5, yy, lbl, color=col, fontsize=6.8, ha="left",
                va="center", zorder=7)

    ax.axvspan(1e-6, 1.0, color=GREEN, alpha=0.08, lw=0, zorder=0)
    ax.axvspan(1.0, 25, color="#CC3311", alpha=0.08, lw=0, zorder=0)
    ax.axvline(1.0, color="0.4", lw=0.8, ls=":", zorder=3)
    ax.text(1.25, 1.5e-5, "$y>1$: prefixes\ncollide by default", color="#993322",
            fontsize=6.8, ha="left", va="bottom", zorder=7)

    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_xlim(1e-6, 25); ax.set_ylim(1e-6, 2.0)
    ax.set_xlabel("$y = p^2/m$   (expected shared prefix elements per pair)")
    ax.set_ylabel("fraction of pairs surviving as candidates")
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

    ax.fill_between(y, 0, pruned, color=GREEN, alpha=0.35, lw=0, zorder=2)
    ax.fill_between(y, pruned, 1.0, color="#CC3311", alpha=0.28, lw=0, zorder=2)
    ax.plot(y, pruned, color=GREEN, lw=1.8, zorder=5)

    ax.axvline(math.log(2), color=INK, lw=1.0, ls=":", zorder=6)
    ax.text(math.log(2) * 1.12, 0.5, "half pruned\nat $y=\\ln 2$", color=INK,
            fontsize=6.8, ha="left", va="center", zorder=7)

    ax.text(4e-3, 0.42, "work never done\n(pairs pruned)", color="#155c22",
            fontsize=8.5, ha="left", va="center", weight="bold", zorder=7)
    ax.text(4.0, 0.60, "work remaining\n(pairs verified\nexactly)",
            color="#8c2d15", fontsize=8.5, ha="center", va="center",
            weight="bold", zorder=7)

    for yy, lab in ((1e-2, "99% pruned"), (1e-1, "90%")):
        ax.plot([yy], [1 - cand_fraction(yy)], marker="o", ms=3.6, mfc="white",
                mec=GREEN, mew=1.3, zorder=8)
        ax.text(yy * 1.35, 1 - cand_fraction(yy) - 0.035, lab, color="#155c22",
                fontsize=6.8, ha="left", va="top", zorder=8)

    ax.set_xscale("log")
    ax.set_xlim(1e-3, 15); ax.set_ylim(0, 1)
    ax.set_xlabel("$y = p^2/m$   (expected shared prefix elements per pair)")
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
    ax.axhspan(lo, 1.0, color=GREEN, alpha=0.08, lw=0, zorder=0)
    ax.axhspan(1.0, hi, color="#CC3311", alpha=0.08, lw=0, zorder=0)
    ax.axhline(1.0, color=INK, lw=2.0, zorder=5)

    ax.fill_between(d, composed, np.minimum(c, zone), color=GOLD, alpha=0.35,
                    lw=0, zorder=2)
    ax.plot(d, c, color=BLUE, lw=1.6, ls=(0, (5, 2)), zorder=4)
    ax.plot(d, zone, color=PURP, lw=1.6, ls=(0, (1.8, 1.5)), zorder=4)
    ax.plot(d, composed, color="#117733", lw=2.3, zorder=6)

    ax.text(2.5e-2, 0.30, "zone map\nalone", color=PURP, fontsize=7,
            ha="left", va="center", zorder=7)
    ax.text(1.1e-2, 2.5e-3, "prefix\nalone", color=BLUE, fontsize=7,
            ha="left", va="center", zorder=7)
    ax.text(1.1e-3, 6e-6, "composed", color="#117733", fontsize=8.5,
            ha="center", va="top", weight="bold", zorder=7)
    ax.text(1.2e-5, 1.35, f"exhaustive bitmap scan", color=INK, fontsize=7,
            ha="left", va="bottom", weight="bold", zorder=7)
    ax.text(0.055, 3e-9, f"$t={t}$,  $W_z={wz}$,  $m=10^{{6}}$", color="0.4",
            fontsize=6.6, ha="right", va="bottom", zorder=7)

    j = int(np.argmin(np.abs(d - 1e-3)))
    print(f"    at d=1e-3: prefix {c[j]:.4g}, zone {zone[j]:.4g}, "
          f"composed {composed[j]:.4g} (bitmap does {1/composed[j]:.0f}x the work)")

    ax.set_xlim(1e-5, 6e-2); ax.set_ylim(lo, hi)
    ax.set_xlabel("density  $d = |X|/m$")
    ax.set_ylabel("cost  $\\div$  exhaustive bitmap scan")
    ax.yaxis.set_major_locator(LogLocator(base=10, numticks=7))
    ax.grid(True, which="major", axis="y", color="0.9", lw=0.5, zorder=1)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)


def main():
    plt.rcParams.update({"font.size": 10})
    fig, axes = plt.subplots(2, 2, figsize=(9.4, 6.6))
    panel_a(axes[0][0]); panel_b(axes[0][1])
    panel_c(axes[1][0]); panel_d(axes[1][1])
    for ax, t in zip(axes.ravel(), ("b   pruning against the threshold",
                                    "c   one curve, whatever the corpus",
                                    "d   how the work divides",
                                    "e   composed with a zone map")):
        ax.set_title(t, loc="left", fontsize=11, weight="bold")
    fig.tight_layout(pad=0.6, w_pad=2.4, h_pad=1.8)
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
