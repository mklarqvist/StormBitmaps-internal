#!/usr/bin/env python3
"""Figure 2 — expected cost of each representation against density.

Pure analysis: no measurement enters this figure. Costs are expressed in
64-bit words touched, the only unit in which the representations are
comparable, and are obtained by simulating uniformly random sets of each
cardinality (so the EWAH figure does not depend on a closed form still
under review).

Every cost here is proportional to the universe size m, so the curves are
scale-invariant: plotted RELATIVE TO THE DENSE BITMAP they are the same at
every m, and the crossovers are universal constants (S meets the bitmap at
d = 1/w = 1/64 ~ 0.0156, one word touched per element against one word of
bitmap per w positions; R at d ~ 0.0159 and ~ 0.9841). Verified numerically
to four significant figures at m = 1e5 and 1e6. One panel therefore suffices,
and a second universe size would only redraw it.

The bitmap is the horizontal line at 1. Below it is work avoided; above it
is work a fixed bitmap would never have done. Growing m does not change the
shape -- it extends the reachable density range further into the tails, so
the largest attainable saving grows with m.

Run:  .venv-fig/bin/python figures/plot_fig2_expectations.py
Out:  figures/fig2_expectations.pdf  (vector, for \\includegraphics)
      figures/fig2_expectations.png  (raster, for inspection only)
"""
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import LogLocator

# ---- house palette (DISPLAY_ITEMS.md 1.1) ---------------------------------
# Hue is reserved for REPRESENTATION IDENTITY and is never spent on anything
# else. Mechanisms (a zone map, a composition) are references, so they take the
# reserved neutral grey plus a line style; the two reserved accents carry the
# only non-representation axis in the figure, cost against saving.
INK   = "#222222"   # B, the foil
BLUE  = "#4477AA"   # S
GREEN = "#228833"   # R
PURP  = "#AA3377"   # W
GOLD  = "#CCBB44"   # C
GREY  = "#BBBBBB"   # reserved: reference / baseline, never a representation

COST  = "#EE6677"   # reserved accent: work done, or dearer than a bitmap
SAVE  = "#66CCEE"   # reserved accent: work avoided
REF   = "0.55"      # mechanism reference lines: grey + line style + a label
TXT   = "0.35"      # small annotation text: ink grey, never a saturated hue

# House rcParams (DISPLAY_ITEMS.md 1.2). pdf.fonttype 42 is NOT optional --
# without it matplotlib emits Type 3 fonts, which production pipelines reject.
matplotlib.rcParams.update({
    "pdf.fonttype": 42, "ps.fonttype": 42,
    "font.size": 8,
    "figure.facecolor": "white", "savefig.facecolor": "white",
    "axes.grid": False,
    "axes.axisbelow": True,
    "savefig.bbox": "tight", "savefig.pad_inches": 0.02,
})

# The canvas is sized to the real text block (\textwidth = 500.484 pt) so that
# "8 pt in matplotlib" is 8 pt on the page. The figure is included at
# width=\linewidth with no downstream scaling; scaling it there would undo
# every type-size decision below.
FIG_W = 500.484 / 72.27          # inches
FIG_H = FIG_W / 1.95

WORD = 64           # bits per machine word
REPEATS = 3
N_STEPS = 100       # 1 bit, 98 linear steps, m-1 bits


def cardinalities(m, spacing="logit"):
    """Sample cardinalities from one set bit to all-but-one set bit.

    `linear`  1 bit, 98 evenly spaced densities, m-1 bits.
    `logit`   the same endpoints, but spaced evenly in log-odds so both tails
              are resolved.  A linear axis puts every crossover inside the
              leftmost 2% of the panel, which is where the figure's whole
              argument lives, so linear spacing is kept only as a cross-check.
    """
    if spacing == "linear":
        mid = np.round(np.linspace(0.0, 1.0, N_STEPS)[1:-1] * m).astype(np.int64)
    else:
        lo = np.log10((1 / m) / (1 - 1 / m))
        t = np.linspace(lo, -lo, N_STEPS - 2)
        mid = np.round(m / (1 + 10.0 ** (-t))).astype(np.int64)
    ks = np.concatenate(([1], np.clip(mid, 1, m - 1), [m - 1]))
    return np.unique(ks)


def runs_and_ewah(bits):
    """Number of maximal runs of 1s, and the EWAH word count.

    EWAH: the universe is cut into WORD-bit words. A word that is all-zero or
    all-one is a fill; consecutive fills of the same kind collapse into one
    marker word. Every other word is a literal and costs one word.
    """
    n_runs = int(bits[0]) + int(np.count_nonzero(bits[1:] & ~bits[:-1]))

    pad = (-bits.size) % WORD          # a real encoder pads the last word
    if pad:
        bits = np.concatenate([bits, np.zeros(pad, dtype=bool)])
    pops = bits.reshape(-1, WORD).sum(axis=1)
    kind = np.where(pops == 0, 0, np.where(pops == WORD, 1, 2))  # 0/1 fill, 2 literal
    # collapse consecutive equal fill kinds; literals never collapse
    boundary = np.empty(kind.size, dtype=bool)
    boundary[0] = True
    boundary[1:] = (kind[1:] != kind[:-1]) | (kind[1:] == 2)
    n_ewah = int(np.count_nonzero(boundary))
    return n_runs, n_ewah


def measure(m, rng, spacing):
    ks = cardinalities(m, spacing)
    R = np.zeros(len(ks))
    W = np.zeros(len(ks))
    scratch = np.zeros(m, dtype=bool)
    for i, k in enumerate(ks):
        acc_r = acc_w = 0
        for _ in range(REPEATS):
            scratch[:] = False
            scratch[:k] = True
            rng.shuffle(scratch)
            r, w = runs_and_ewah(scratch)
            acc_r += r
            acc_w += w
        R[i] = acc_r / REPEATS
        W[i] = acc_w / REPEATS
    return ks, R, W


def panel(ax, m, rng, spacing):
    ks, r_runs, w_words = measure(m, rng, spacing)
    d = ks / m
    bitmap = np.ceil(m / WORD)

    # every curve as a multiple of the dense-bitmap cost: dimensionless, and
    # identical at every universe size
    # one word touched per element (a probe or a comparison), matching the
    # cost convention of THEORY.md; NOT the u32 storage size k/2.
    c_S = (ks * 1.0) / bitmap
    c_C = (np.minimum(ks, m - ks) * 1.0) / bitmap
    c_R = r_runs / bitmap
    c_W = w_words / bitmap

    ax.set_yscale("log")
    lo, hi = 2e-5, max(c_S.max(), c_R.max()) * 3.0

    # The half-plane wash marks position relative to the BITMAP, which is
    # achromatic, so the wash is achromatic too: tinting it green would put an
    # R-coloured field under the R curve, spending a representation hue on a
    # non-representation meaning.
    ax.axhspan(1.0, hi, color=GREY, alpha=0.35, lw=0, zorder=0)
    ax.axhline(1.0, color=INK, lw=2.4, zorder=5)

    # Structure-dependent representations are drawn as RANGES, not curves:
    # at a fixed density the cost of R and W depends on arrangement, and the
    # i.i.d. curve is the worst case (scattering maximises run count). The
    # best case is one run. That band is Theorem B, drawn.
    r_best = np.full_like(d, 1.0) / bitmap                 # one run
    w_best = np.full_like(d, 3.0) / bitmap                 # fill, literal, fill
    ax.fill_between(d, r_best, c_R, color=GREEN, alpha=0.10, lw=0, zorder=1)

    # On the sparse side S, C and R coincide (every representation costs about
    # one word per element), so direct labelling is impossible here and a
    # frameless legend is the readable choice. It doubles as the greyscale key:
    # each representation carries its own dash pattern as well as its own hue.
    ax.plot(d, c_S, color=BLUE,  lw=1.6, zorder=4, label="S  sorted array")
    ax.plot(d, c_R, color=GREEN, lw=1.6, ls=(0, (5, 1.6)), zorder=4,
            label="R  runs")
    ax.plot(d, c_W, color=PURP,  lw=1.8, ls=(0, (1.6, 1.4)), zorder=6,
            label="W  EWAH fills")
    ax.plot(d, c_C, color=GOLD,  lw=1.6, ls=(0, (5, 2, 1, 2)), zorder=4,
            label="C  complement")
    ax.plot(d, r_best, color=GREEN, lw=1.0, alpha=0.8, zorder=4)
    # These two labels annotate R's own band, so R's hue is the correct one.
    ax.text(2.5e-3, r_best[0] * 2.2, "one run (best case)", color=GREEN,
            fontsize=7.0, ha="center", va="bottom", zorder=7)
    ax.text(0.5, 2.2e-3, "range of R at fixed density", color=GREEN,
            fontsize=7.2, ha="center", va="center", style="italic", zorder=7)
    # Two columns, so the key is two rows tall and clears the bitmap line and
    # the crossover labels that sit just beneath it.
    # Anchored in the only genuinely empty region of the panel: above the
    # bitmap line and left of where any curve rises to meet it.
    leg = ax.legend(loc="upper left", frameon=False, fontsize=6.5, ncol=1,
                    handlelength=1.8, borderpad=0.05, labelspacing=0.22,
                    handletextpad=0.45, bbox_to_anchor=(-0.015, 1.03))
    leg.set_zorder(9)

    # crossovers: where each representation stops being cheaper than a bitmap.
    # These are universal constants -- independent of m -- because every cost
    # is proportional to m.
    def crossings(y):
        out = []
        for i in range(len(y) - 1):
            if (y[i] - 1.0) * (y[i + 1] - 1.0) < 0:
                f = (1.0 - y[i]) / (y[i + 1] - y[i])
                out.append(d[i] + f * (d[i + 1] - d[i]))
        return out

    marks = []
    for name, y, col in (("S", c_S, BLUE), ("R", c_R, GREEN),
                         ("W", c_W, PURP), ("C", c_C, GOLD)):
        for x in crossings(y):
            marks.append((name, x, col))
    for name, x, col in marks:
        ax.plot([x], [1.0], marker="o", ms=4.2, mfc="white", mec=col,
                mew=1.4, zorder=8, clip_on=False)
    print("  crossovers (cost = dense bitmap):")
    for name, x, _ in sorted(marks, key=lambda t: t[1]):
        print(f"    {name}  d = {x:.4f}")

    def dedupe(ms):
        out = []
        for n, x, c in sorted(ms, key=lambda t: t[1]):
            if out and abs(out[-1][1] - x) < 1e-4:
                out[-1] = (out[-1][0] + "," + n, out[-1][1], out[-1][2])
            else:
                out.append((n, x, c))
        return out
    lo_marks = dedupe([m_ for m_ in marks if m_[1] < 0.5])
    hi_marks = dedupe([m_ for m_ in marks if m_[1] > 0.5])
    if lo_marks:
        ax.text(lo_marks[0][1] * 0.55, 1.0 / 150.0,
                "   ".join(f"{n} {x:.4f}" for n, x, _ in lo_marks),
                fontsize=7.0, color=TXT, ha="right", va="center", zorder=8)
    if hi_marks:
        ax.text(min(hi_marks[-1][1] * 1.0008, 0.9999), 1.0 / 150.0,
                "   ".join(f"{n} {x:.4f}" for n, x, _ in hi_marks),
                fontsize=7.0, color=TXT, ha="left", va="center", zorder=8)

    ax.set_ylim(lo, hi)
    if spacing == "logit":
        ax.set_xscale("logit")
        ax.set_xlabel("density  $d = |X|/m$   (log-odds axis, symmetric about $d = 0.5$)")
    else:
        ax.set_xlim(0, 1)
        ax.set_xlabel("density  $d = |X|/m$")
    ax.axvline(0.5, color="0.75", lw=0.6, ls=":", zorder=1)
    ax.set_ylabel("cost $\\div$ dense bitmap")
    ax.yaxis.set_major_locator(LogLocator(base=10, numticks=12))
    ax.grid(True, which="major", axis="y", color="0.9", lw=0.5, zorder=1)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)


def zone_panel(ax, spacing, wz=512):
    """Zone map cost relative to a plain bitmap pairing, as a BAND.

    Cost/plain = 1/Wz + p_A p_B, where p is the fraction of bins a set
    occupies. p depends on how the set is arranged, not only on its density,
    and the two extremes are both closed-form:

      scattered (i.i.d.)   p = 1 - (1-d)^Wz     -- the WORST case: random
                                                   scattering maximises bin
                                                   occupancy for a given d
      clustered            p = d                 -- runs longer than a bin
                                                   occupy exactly d of them

    The band between them is what structure is worth, before any measurement.
    At d = 0.01 and Wz = 512 it spans a factor of ~480.
    """
    d = np.concatenate([np.logspace(-6, np.log10(0.5), 500),
                        1 - np.logspace(np.log10(0.5), -6, 500)[1:]])
    p_iid = 1.0 - (1.0 - d) ** wz
    worst = 1.0 / wz + p_iid * p_iid
    best  = 1.0 / wz + d * d

    ax.set_yscale("log")
    lo, hi = 5e-4, 3.0
    # The half-plane wash marks position relative to the BITMAP, which is
    # achromatic, so the wash is achromatic too: tinting it green would put an
    # R-coloured field under the R curve, spending a representation hue on a
    # non-representation meaning.
    ax.axhspan(1.0, hi, color=GREY, alpha=0.35, lw=0, zorder=0)
    ax.axhline(1.0, color=INK, lw=2.4, zorder=5)

    # A zone map is a MECHANISM, not a representation, so it must not borrow
    # W's purple. The saving it buys is the reserved "work avoided" accent, and
    # the two arrangements are separated by line style, not by hue.
    ax.fill_between(d, best, worst, color=SAVE, alpha=0.35, lw=0, zorder=2)
    ax.plot(d, worst, color=REF, lw=1.6, ls=(0, (1.8, 1.5)), zorder=4)
    ax.plot(d, best,  color=REF, lw=1.6, zorder=4)
    ax.axhline(1.0 / wz, color="0.45", lw=0.7, ls=":", zorder=3)

    ax.text(6e-3, 0.42, "scattered\n(i.i.d.)", color=TXT, fontsize=7.2,
            ha="left", va="center", zorder=7)
    ax.text(0.30, 3.0e-3, "clustered", color=TXT, fontsize=7.2,
            ha="right", va="top", weight="bold", zorder=7)
    ax.text(1.3e-2, 3.6e-2, "what structure\nis worth", color=TXT,
            fontsize=7.2, ha="center", va="center", style="italic", zorder=7)
    ax.text(1.5e-6, 1.0 / wz * 1.35, "$1/W_z$ floor", color="0.45",
            fontsize=7.0, ha="left", va="bottom", zorder=7)
    ax.text(1.5e-6, 1.45, "B   plain dense bitmap", color=INK, fontsize=7.6,
            ha="left", va="bottom", weight="bold", zorder=7)

    j = int(np.argmin(np.abs(d - 0.01)))
    print(f"    Wz={wz}: at d=0.01 scattered={worst[j]:.4f}, clustered={best[j]:.5f}"
          f"  ->  {worst[j]/best[j]:.0f}x apart")

    ax.set_ylim(lo, hi)
    if spacing == "logit":
        ax.set_xscale("logit")
        ax.set_xlabel("density  $d = |X|/m$   (log-odds axis)")
    else:
        ax.set_xlim(0, 1)
        ax.set_xlabel("density  $d = |X|/m$")
    ax.axvline(0.5, color="0.75", lw=0.6, ls=":", zorder=1)
    ax.set_ylabel("cost $\\div$ plain bitmap")
    ax.yaxis.set_major_locator(LogLocator(base=10, numticks=8))
    ax.grid(True, which="major", axis="y", color="0.9", lw=0.5, zorder=1)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)


def kappa_best(x, floor=0.0):
    """Cheapest single representation at density x, relative to a bitmap.

    Analytic, from the same expressions the other panels use, with w = 64:
      B 1;  S 64x;  C 64 min(x,1-x);  R 64 x(1-x);  W 1-(1-x)^128-x^128.
    """
    x = np.clip(x, 1e-12, 1 - 1e-12)
    cands = np.vstack([
        np.ones_like(x),
        64.0 * x,
        64.0 * np.minimum(x, 1 - x),
        64.0 * x * (1 - x),
        1.0 - (1 - x) ** 128 - x ** 128,
    ])
    # A representation always costs at least one word, however degenerate the
    # set: without this floor the complement of a full set costs zero, which
    # inverts the best/worst band at the dense extreme.
    return np.maximum(cands.min(axis=0), floor)


def combo_panel(ax, spacing, wz=512):
    """Representation, zone map, and the two composed -- each as a best-worst band.

    For every mechanism the i.i.d. arrangement is the WORST case at a given
    density and perfect clustering the best, so each is a band rather than a
    curve:

      representation   best: one run (1 word)      worst: kappa_iid(d)
      zone map         best: p = d                 worst: p = 1-(1-d)^Wz
      composed         best: 1/Wz + d^2 kappa(1)   worst: 1/Wz + p^2 kappa(d/p)

    Filtering concentrates density: conditioned on a bin being occupied its
    local density is d/p, so the representation used inside surviving bins is
    the one for d/p, not for d.
    """
    d = np.concatenate([np.logspace(-7, np.log10(0.5), 600),
                        1 - np.logspace(np.log10(0.5), -7, 600)[1:]])
    p = 1.0 - (1.0 - d) ** wz
    one_word = 64.0 / (1 << 20)
    k_full = 64.0 / wz                      # a full bin is one run

    repr_w = kappa_best(d, floor=one_word)
    repr_b = np.full_like(d, one_word)
    zone_w = 1.0 / wz + p * p;           zone_b = 1.0 / wz + d * d
    # inside a bin the unit is one word out of Wz/64, so the floor is 64/Wz
    comp_w = 1.0 / wz + p * p * kappa_best(np.clip(d / p, 0, 1), floor=k_full)
    comp_b = 1.0 / wz + d * d * k_full

    # Same absolute axis as (a) and (b) so the three panels can be scanned
    # across. What matters is the DEPTH below the bitmap line -- orders of
    # magnitude -- not the margin over the runner-up.
    ax.set_yscale("log")
    lo, hi = 2e-5, 3.0
    # The half-plane wash marks position relative to the BITMAP, which is
    # achromatic, so the wash is achromatic too: tinting it green would put an
    # R-coloured field under the R curve, spending a representation hue on a
    # non-representation meaning.
    ax.axhspan(1.0, hi, color=GREY, alpha=0.35, lw=0, zorder=0)
    ax.axhline(1.0, color=INK, lw=2.4, zorder=5)

    # The two mechanisms alone are REFERENCES, so they take the reserved grey
    # and are told apart by line style and a direct label -- not by borrowing
    # S's blue and W's purple, which name representations elsewhere.
    ax.plot(d, repr_w, color=REF, lw=1.1, zorder=3)
    ax.plot(d, zone_w, color=REF, lw=1.1, ls=(0, (1.8, 1.5)), zorder=3)

    # the composition -- the result -- as a best-worst band in the "avoided" accent
    ax.fill_between(d, comp_b, comp_w, color=SAVE, alpha=0.40, lw=0, zorder=2)
    ax.plot(d, comp_w, color=SAVE, lw=2.4, zorder=6)
    ax.plot(d, comp_b, color=SAVE, lw=1.2, ls=(0, (3, 2)), zorder=6)

    # Depth markers. These state a SHARE of the bitmap's work, never a
    # "N times less work" ratio, which has no well-defined referent.
    for depth, lab in ((1e-1, "$10\\%$ of a bitmap"),
                       (1e-2, "$1\\%$"),
                       (1e-3, "$0.1\\%$")):
        ax.axhline(depth, color="0.75", lw=0.5, ls=":", zorder=1)
        ax.text(0.9999, depth * 1.25, lab, color="0.45", fontsize=6.8,
                ha="right", va="bottom", zorder=7)

    # A frameless three-entry key replaces three free-floating labels that
    # collided once the panel was set at its true printed height. The
    # solid/dashed convention is stated in the caption.
    ax.plot([], [], color=REF, lw=1.1, label="representation alone")
    ax.plot([], [], color=REF, lw=1.1, ls=(0, (1.8, 1.5)), label="zone map alone")
    ax.plot([], [], color=SAVE, lw=2.4, label="composed")
    leg = ax.legend(loc="lower left", frameon=False, fontsize=6.8,
                    handlelength=1.9, borderpad=0.1, labelspacing=0.25,
                    handletextpad=0.5, bbox_to_anchor=(-0.01, -0.02))
    leg.set_zorder(9)

    # Values quoted in the caption are printed here so the two cannot drift.
    j = int(np.argmin(np.abs(d - 1e-3)))
    print(f"    at d=1e-3 composed costs {100*comp_w[j]:.2f}% of a bitmap "
          f"(a bitmap does {1/comp_w[j]:.0f}x the work)")
    print(f"    clustered floor 1/Wz = {100/wz:.3f}% of a bitmap; "
          f"clustered cost at the dense end = {100*comp_b[-1]:.1f}%; "
          f"knee where d^2*(64/Wz) = 1/Wz, i.e. d = 1/8, for every Wz")

    ax.set_ylim(lo, hi)
    if spacing == "logit":
        ax.set_xscale("logit")
        ax.set_xlabel("density  $d = |X|/m$   (log-odds axis)")
    else:
        ax.set_xlim(0, 1)
        ax.set_xlabel("density  $d = |X|/m$")
    ax.axvline(0.5, color="0.75", lw=0.6, ls=":", zorder=1)
    ax.set_ylabel("cost $\\div$ plain bitmap")
    ax.yaxis.set_major_locator(LogLocator(base=10, numticks=8))
    ax.grid(True, which="major", axis="y", color="0.9", lw=0.5, zorder=1)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)


def split_panel(ax, spacing, wz=512):
    """How the bitmap's work divides: deleted vs still done.

    Linear 0-1 axis, because the question is what share of a fixed-cost
    kernel's work survives. The envelope is the best of {bitmap, best
    representation, zone-mapped bitmap, the two composed}; everything above it
    is work a bitmap would have done and an adaptive kernel does not.
    """
    d = np.concatenate([np.logspace(-7, np.log10(0.5), 600),
                        1 - np.logspace(np.log10(0.5), -7, 600)[1:]])
    p = 1.0 - (1.0 - d) ** wz
    k_full = 64.0 / wz
    one_word = 64.0 / (1 << 20)
    env = np.minimum.reduce([
        np.ones_like(d),                                             # bitmap
        kappa_best(d, floor=one_word),                               # representation
        1.0 / wz + p * p,                                            # zone map
        1.0 / wz + p * p * kappa_best(np.clip(d / p, 0, 1), floor=k_full),
    ])
    # The two reserved accents, on the figure's one non-representation axis:
    # red = work still done, cyan = work avoided. The same pair carries the
    # same meaning in panel a, so no colour changes sense across the figure.
    done = np.clip(env, 0, 1)
    ax.fill_between(d, 0, done, color=COST, alpha=0.45, lw=0, zorder=2)
    ax.fill_between(d, done, 1.0, color=SAVE, alpha=0.45, lw=0, zorder=2)
    ax.plot(d, done, color=COST, lw=1.8, zorder=5)

    ax.text(1.3e-6, 0.30, "work never done", color=TXT, fontsize=8,
            ha="left", va="center", weight="bold", zorder=7)
    ax.text(0.5, 0.66, "work still\ndone", color=TXT, fontsize=7,
            ha="center", va="center", weight="bold", zorder=7)
    ax.text(1.3e-6, 0.95, "a fixed-cost bitmap does all of it", color=INK,
            fontsize=7.2, ha="left", va="top", zorder=7)
    ax.axhline(1.0, color=INK, lw=2.0, zorder=6)

    frac = 1.0 - done
    for target in (1e-4, 1e-2):
        j = int(np.argmin(np.abs(d - target)))
        print(f"    at d={target:g}: {100*frac[j]:.3f}% of the bitmap's work is never done")

    ax.set_ylim(0, 1.0); ax.set_yticks([0, 0.25, 0.5, 0.75, 1.0])
    if spacing == "logit":
        ax.set_xscale("logit")
        ax.set_xlabel("density  $d = |X|/m$   (log-odds axis)")
    else:
        ax.set_xlim(0, 1)
        ax.set_xlabel("density  $d = |X|/m$")
    ax.axvline(0.5, color="0.75", lw=0.6, ls=":", zorder=1)
    ax.set_ylabel("share of a bitmap's work")
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)


def build(spacing, stem):
    rng = np.random.default_rng(20260805)
    fig, axes = plt.subplots(2, 2, figsize=(FIG_W, FIG_H))
    print("  panel a:")
    panel(axes[0][0], 1_048_576, rng, spacing)
    print("  panel b:")
    zone_panel(axes[0][1], spacing)
    print("  panel c:")
    combo_panel(axes[1][0], spacing)
    print("  panel d:")
    split_panel(axes[1][1], spacing)
    # Panel letters match the LaTeX figure, whose panel a is the TikZ
    # schematic set above this canvas. 9 pt bold, the same size and weight as
    # the "a" that LaTeX sets, so no panel letter is larger than its neighbour.
    for ax, t in zip(axes.ravel(), ("b   representations", "c   zone maps",
                                    "d   both, composed",
                                    "e   how the work divides")):
        ax.set_title(t, loc="left", fontsize=9, weight="bold", pad=4)
    fig.tight_layout(pad=0.4, w_pad=2.2, h_pad=2.0)
    for ext in ("pdf", "png"):
        fig.savefig(f"figures/{stem}.{ext}", dpi=220, bbox_inches="tight")
    print(f"wrote figures/{stem}.pdf/.png")


def main():
    build("logit",  "fig2_expectations")
    build("linear", "fig2_expectations_linear")


if __name__ == "__main__":
    main()
