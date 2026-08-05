# Display Item Specification — StormBitmaps paper

Written against `NARRATIVE.md` (thesis, claim ledger, section skeleton) and
`PROBLEM_STATEMENT.md`. `results/` is empty; the campaign is re-running. **No
numeric value in this document is to be typeset as data** — every number below
is either a placeholder axis label, a source-code line count, or a schema
field name, never a measurement. Where NARRATIVE.md's own tables carry
placeholder numbers (marked as such at its §4 preamble), this document does
not repeat them.

Companion skills consulted: `paper-figures` (chart/table house style),
`simd-lane-figures` (schematic construction via `paper/simdgrid.sty`).

---

## 0. Proposed changes to the six-item inventory — read this first

NARRATIVE.md §6 fixes six main items: schematic, density sweep, two-panel
decay figure, selection-cost figure, corpus table, storage table. Building
each one out in full surfaced two structural problems with that allocation.
Both are argued below; §§1–6 spec the inventory **as revised**. If the
revision is rejected, Table 2 reverts to the storage table using the schema
in §8 (S1), and Fig. 4 drops back to two panels by deleting §4's panel (c).

### Change 1 — C11 (Roaring's fixed-chunk mispricing) has no display item and needs one

R5 is a full Results subsection ("Fixed chunk granularity misprices dense
corpora") built entirely on C11 — the container census
(`RESEARCH_PLAN.md` §15.3) showing `census1881` has **zero** bitset
containers at 1.2×10⁻³ global density because Roaring's per-chunk threshold
test never fires. NARRATIVE.md itself calls this "the sharpest single piece
of evidence we have" (§3) and "a concrete, mechanistic, observed-not-inferred
weakness in a library deployed everywhere." Yet the six-item table assigns it
no figure and no table. A reader reaches R5 and is asked to believe a claim
about container internals with nothing to look at — the one Results
subsection whose whole argument is "look at the competitor's own data
structure" has no data structure on the page.

**Proposal:** promote the container-census table to main-text Table 2,
replacing the storage table. See §7 for the new spec.

### Change 2 — the storage table moves to Supplementary, not out of the paper

This is what makes room for Change 1 inside the fixed budget of six. The
justification is not "storage doesn't matter" — A10/L3 are real, load-bearing
claims. It is that `PROBLEM_STATEMENT.md` §8 states explicitly: *"Where the
two conflict, speed wins; where storage matters, it is a reported trade-off,
not an objective."* Storage is background, stated once in prose (Discussion,
per NARRATIVE.md §5's Discussion paragraph on the artifact gap, and R6 for
L3), not a claim requiring its own main-text axis the way R1–R6 all argue
about time. The Roaring papers themselves run bits-per-value tables in an
appendix/secondary table, not their headline Table 1. Demoted to S1 (§8),
with the full schema retained so nothing is lost — only its shelf.

### Change 3 — A9 (gating necessity) folds into Fig. 4 as a third panel, not a new item

R3's claim list is A5, A6, **A9**, but the display-item table gives Fig. 4
only A5 and A6. A9 ("work avoidance must itself be gated or it is a net
loss") is a one-line but structurally important finding — unconditional
zone-map application is an *11-of-25-corpora regression*, which is exactly
the finding that justifies applying work avoidance conditionally, and so
one of the three that NARRATIVE.md §2 admits to the manuscript. Rather than spend a seventh main item on it, Fig. 4 becomes a
three-panel figure: cost, regret, gating-necessity. All three are "the cost
and correctness of deciding," which is one coherent panel triplet, not three
unrelated ideas glued together — see §4.

### What is *not* proposed to change

Fig. 1 (schematic), Fig. 2 (density sweep) and Table 1 (real corpora) are
correctly placed and sized; §§1, 2, 5 spec them without structural argument.
Fig. 3's two-panel split (A1/A8 decay vs. work-avoidance growth) is also kept
— see §3 for how A1's "0 of 25" is rendered without a dedicated panel.

---

## 1. Global conventions — one system for every display item

Apply once, here; every subsequent section just says "house palette" or
"house rcParams" and means this.

### 1.1 Colour and mark system

Five representations (`B`, `S`, `R`, `W`, `C`) plus two "not us" references
(CRoaring / the all-bitmap baseline). Two encodings never collapse into one:
**hue = representation identity, filled-vs-open / split-marker = which side
of a pair is which.** This mapping is used in matplotlib figures, in
`simdgrid.sty` lane strips (via `\colorlet` overrides), and — where a table
ever needs to flag a winning representation family — as a coloured token
before the cell name.

| Symbol | Representation | Hex | Role |
|---|---|---|---|
| **B** | Dense bitmap | `#222222` (ink) | The foil. Achromatic on purpose — it is the fixed-cost default being displaced, not a strategy competing on equal footing with the other four. |
| **S** | Sorted array | `#4477AA` (blue) | |
| **R** | Run/RLE | `#228833` (green) | |
| **W** | WAH/EWAH fill | `#AA3377` (purple) | |
| **C** | Complement | `#CCBB44` (gold) | Dense-tail strategy (B5); appears mainly in Fig. 1 and Supplementary, not the main density sweep, which is defined over the ten non-`C` cells. |
| *(ref)* | CRoaring / all-bitmap baseline | `#BBBBBB` (neutral grey) | Reserved exclusively for "not our representation set." Never reused for `B`, `S`, `R`, `W`, or `C`, so grey always means "competitor or trivial baseline" at a glance. |

Source: Paul Tol's "bright" qualitative palette (`#4477AA #66CCEE #228833
#CCBB44 #EE6677 #AA3377 #BBBBBB`), independently vetted for deuteranopia,
protanopia and tritanopia — five of its seven slots are used above; `#66CCEE`
(cyan) is held in reserve for a sixth series if one is ever needed (e.g. a
"model prediction" line distinct from both oracle and measurement), and
`#EE6677` (red) is held in reserve for an explicit error/failure marker
(e.g. "harmed" corpora in the A9 panel) rather than reused from the
representation set.

**Greyscale check.** Luminance order is `B` (darkest) → `R` → `S` → `W` →
`C` (lightest) — verify with a Coblis / grayscale-simulation pass before
final submission. Because hue is stripped in greyscale print, **every figure
in this paper must also carry a second, non-colour encoding** (marker shape,
line style, or direct label) so no panel depends on colour alone. That
second encoding is specified per pairing cell below.

**Pairing-cell marks** (10 cells + the `C×B` strategy cell, which
`NARRATIVE.md` §7b requires to be visually distinguished from the ten
symmetric cells, not silently folded in):

| Cell | Hue | Fill | Line | Marker |
|---|---|---|---|---|
| B×B | `B` ink | filled | solid, 3pt (heaviest line in every figure — this is always the reference curve) | square |
| B×S | `S` blue | **open/hollow** ring | solid | circle |
| B×R | `R` green | open ring | solid | triangle-up |
| B×W | `W` purple | open ring | solid | diamond |
| S×S | `S` blue | filled | dashed | circle |
| R×R | `R` green | filled | dashed | triangle-up |
| W×W | `W` purple | filled | dashed | diamond |
| S×R | split blue/green (diagonal, `\simdsplit`-style half-marker) | — | dotted | triangle-down |
| S×W | split blue/purple | — | dotted | triangle-down |
| R×W | split green/purple | — | dotted | triangle-down |
| C×B (strategy, not a symmetric cell) | `C` gold | filled, **starred outline** | solid | star |

Rule stated once, applied everywhere: **"B present" cells are hollow rings;
"B absent, same representation twice" cells are filled with a dashed line;
"B absent, two different representations" cells are split bicolour markers
with a dotted line.** A reader who has seen this key once can read every
subsequent chart without consulting the legend, which is the point of
direct-labeling house style.

### 1.2 matplotlib rcParams (all matplotlib-built figures)

```python
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

matplotlib.rcParams.update({
    "pdf.fonttype": 42, "ps.fonttype": 42,
    "font.size": 9,
    "figure.facecolor": "white", "savefig.facecolor": "white",
    "axes.grid": False,
    "axes.axisbelow": True,
    "savefig.bbox": "tight", "savefig.pad_inches": 0.02,
})
```

Two spines only (despine top/right), outward ticks, minor ticks on log axes,
subtle y-grid only (`color=0.6, alpha=0.25`), panel letters bold lowercase
top-left. Size every canvas to real column width in points
(`figsize = (width_pt/72.27, width_pt/72.27/1.618)`); do not scale a larger
figure down in `\includegraphics`.

### 1.3 File layout

All generators live in `paper/figures/gen/`, all output PDFs in
`paper/figures/`, one `.py` per figure registered in a single
`FIGURES = [...]` list at the bottom of a shared driver
`paper/figures/gen/make_all.py` (new file — none of this exists yet). Each
function's docstring names its source artifact path(s) exactly, per the
`paper-figures` provenance rule. Native-TikZ items (Fig. 1) live directly in
`main.tex` or a `paper/figures/fig1_schematic.tex` `\input`.

---

## 2. Fig. 1 — The pairing matrix (schematic)

**Claim carried:** there exists a matrix of representation-pairing cells,
each with its own cost function, and a selector that walks metadata to a
kernel choice — this is the object the entire paper studies, stated once,
visually, before any measurement appears.

### Panels

Single panel, three visually stacked zones read top-to-bottom:

1. **The matrix itself** — a 5×5 grid (rows/cols = `B, S, R, W, C`) of cells,
   upper-triangular + diagonal populated (10 cells), each cell shown as a
   small lane-strip icon built from `simdgrid.sty` (e.g. `B×S` shown as a
   solid bitmap strip next to a sparse dotted array strip — literally two
   `\simdstrip` rows of contrasting fill density). `C×B` sits outside the
   grid as an eleventh, separately bordered cell (star marker per §1.1),
   consistent with `NARRATIVE.md` §7b's instruction that it is "a strategy,
   not a symmetric cell." Two cells in the grid (the ones without kernels —
   Ro rows/cols) are absent entirely: **do not draw a Roaring row.**
   `NARRATIVE.md` §7b is explicit that there is no Roaring meta-cell.
2. **One annotated cost function** per cell family, as small math labels
   under three representative cells (B×B: Θ(m); B×R: Θ(r); S×S: Θ(\|A\|+\|B\|))
   — not all ten, to avoid clutter; the caption states the rest are in
   Methods Table (the cost-function table already exists as prose in
   `PROBLEM_STATEMENT.md` §3).
3. **The selection path** — a small flow strip below the matrix: metadata
   icons (cardinality, run count, occupancy bitmask — three small labeled
   boxes) → an arrow into a single decision diamond → an arrow to "chosen
   cell" pointing back up into the matrix. This part is plain box-and-arrow
   TikZ (per `simd-lane-figures` scope note: abstract dataflow is not this
   kit's job), not `simdgrid.sty`.

No axes, no log/linear question — this item carries no measured data.

### Data schema

None. This figure is drawn from the taxonomy already fixed in
`PROBLEM_STATEMENT.md` §3 and `NARRATIVE.md` §7b (five representations, ten
cells, one strategy cell). Nothing here waits on the campaign — it can be
built today.

### Draft caption (T-P-D-S)

> **Fig. 1 | The representation-pairing matrix.** **a**, Five representation
> families — dense bitmap (B), sorted array (S), run/RLE (R), WAH/EWAH fill
> (W) and complement (C) — form ten pairwise kernels (upper triangle
> including the diagonal); each cell has a distinct asymptotic cost in its
> own driving variable (word count for B×B, run count for B×R, combined
> cardinality for S×S; full list in Methods). C×B (starred, outside the
> grid) is a strategy applied above density ½ rather than an eleventh
> symmetric cell. **b**, The selection path: per-pair metadata (cardinality,
> run count, chunk-occupancy bitmask), computed once at build time, is
> consulted by a cost model that returns the predicted-cheapest cell. No
> raw bits are read during selection. Schematic; carries no measured data.

### Construction route

Native TikZ, `paper/simdgrid.sty` for the matrix-cell icons +
`paper/figures/fig1_schematic.tex`, `\input`-ed from `main.tex`.

**TikZ structure sketch:**

```latex
% paper/figures/fig1_schematic.tex
\begin{figure}[!t]\centering
\resizebox{\linewidth}{!}{%
\begin{tikzpicture}[
  cellbox/.style={draw=black!55, line width=0.5pt, minimum width=2.1cm,
                  minimum height=1.5cm, inner sep=2pt},
  repcolor/.style={}, % filled per-node using \colorlet from simdgrid palette
]
  % ---- 5x5 grid scaffold: row/col headers B S R W C ----
  \foreach \r [count=\ri from 0] in {B,S,R,W,C}
    \node[simd label] at (-1, -\ri) {\r};
  \foreach \c [count=\ci from 0] in {B,S,R,W,C}
    \node[simd label] at (\ci, 0.6) {\c};

  % ---- upper-triangular + diagonal cells, each a tiny simdpicture inset ----
  % B x B  (row 0, col 0)
  \begin{scope}[shift={(0,0)}]
    \begin{simdpicture}[scale=0.16]
      \simdstrip{0}{}{1/simdBcolor,1/simdBcolor,1/simdBcolor,1/simdBcolor,
                       1/simdBcolor,1/simdBcolor,1/simdBcolor,1/simdBcolor}
    \end{simdpicture}
    \node[simd note, below=1pt] {$\Theta(m)$};
  \end{scope}
  % B x S (row 0, col 1): dense strip above, sparse dotted strip below
  \begin{scope}[shift={(1,0)}]
    \begin{simdpicture}[scale=0.16]
      \simdstrip{1}{}{1/simdBcolor,1/simdBcolor,1/simdBcolor,1/simdBcolor}
      \simdstrip{0}{}{0/white,1/simdScolor,0/white,0/white}
    \end{simdpicture}
  \end{scope}
  % ... repeat per cell (B x R, B x W, S x S, S x R, S x W, R x R, R x W, W x W)
  % C x B: separate box, dashed border, star marker, placed below-right of grid
  \node[cellbox, dashed, draw=simdF] (cxb) at (6.5,-2.5) {$C \times B$};
  \node[simd note] at (6.5,-3.1) {strategy, $d>\nicefrac{1}{2}$ — not a symmetric cell};

  % ---- selection path strip, below the matrix ----
  \node[cellbox] (meta) at (2, -6.5) {metadata\\[2pt]\scriptsize card., runs, occ.};
  \node[draw, diamond, aspect=2] (dec) at (5, -6.5) {cost\\model};
  \node[cellbox] (chosen) at (8, -6.5) {chosen cell};
  \draw[-Latex] (meta) -- (dec);
  \draw[-Latex] (dec) -- (chosen);
  \draw[-Latex, dashed] (chosen.north) to[out=90,in=-90] (2,-0.5);
\end{tikzpicture}}
\caption{...}
\label{fig:matrix}
\end{figure}
```

Iterate this on a `standalone` harness per `simd-lane-figures` workflow before
wiring into `main.tex`; render and visually inspect — lane grids fail
silently on a mis-signed macro argument.

---

## 3. Fig. 2 — Density sweep

**Claim carried:** intersection cost is U-shaped in density; the operative
variable is distance from ½, not sparsity; B×B is flat (fixed cost) across
five orders of magnitude while every other cell slopes.

### Panels

Three panels, stacked vertically (matches the existing draft generator's
layout — see Construction route):

- **(a) Cost vs. density.** X: density (log, both sides equal — this sweep
  is uniform/uniform only, per `NARRATIVE.md` §7b's caveat that the 1/i data
  lives elsewhere). Y: ns/pair (log). One line per cell (house palette/marks,
  §1.1), plus a **separate dotted grey reference line** for the pure-SIMD
  fixed-cost B×B kernel (`variant == "neon_u8"` or equivalent per-host
  baseline; kept visually distinct from the *best* B×B line, which may use
  the zone-map-accelerated variant and therefore is not flat — the whole
  point of A2 is that the *fixed-cost* reference and the *best achievable*
  B×B are two different curves). Shaded background bands mark the three
  regimes (sparse-wins / B×B-wins / dense-wins-by-symmetry), with the
  crossover densities as vertical reference lines, labelled with their
  measured value (not hard-coded — read off the data, as the existing
  generator already does).
- **(b) Speedup vs. density.** Same x-axis. Y: speedup over the fixed-cost
  B×B reference (log), one line per non-B×B cell, horizontal reference at
  1.0×.
- **(c) Winning-variant heat strip.** Y: cell (categorical, 10 rows). X:
  density (same axis, shared with a/b via `sharex`). Colour: categorical
  index into whichever *variant* won at that density/cell — this is the
  literal input the selection model (M2) must reproduce, so it is not
  decorative: it is falsifiable evidence for/against the selector's map.

Panel (a)/(b) log–log; panel (c) categorical y, log x, no y-scale.

### Data schema — checklist

From `bench/bench_cells.cpp --json`, run by `bench/density_sweep.sh`, into
`results/density.jsonl`. One JSON object per line, per (cell, variant,
density) triple:

| Field | Meaning | Used in |
|---|---|---|
| `cell` | e.g. `"B x S"` | panel selection, x/y grouping |
| `variant` | kernel name, e.g. `"ilp8"`, `"neon_u8"` | panel (c) colour/label; panel (a) B×B fixed-cost line filter (`variant == "neon_u8"`) |
| `ns_pair` | measured ns per pair, best-of-N repeats | panel (a)/(b) y |
| `rows`, `universe`, `pairs` | corpus/sample size | caption "n" statement |
| `density` | shared density, both sides | x-axis |
| `structure`, `spectrum` | must be `"uniform"`/`"uniform"` for this sweep — confirm before use, a mixed-spectrum row silently entering this file would misplot | filter/assert |
| `mean_card`, `mean_runs` | corpus shape actually realised at that density | sanity check against the requested density |
| `checksum` | oracle-agreement token | drop any row where the corresponding oracle check failed (script already only appends `r.correct` rows) |
| `inflate_baseline` | boolean | **exclude from "best" selection** — these are labelled baselines, never winners, exactly as the existing loader already enforces |
| `ghz_measured` | per-run clock calibration | caption cache/host statement only, not plotted |

Universe is pinned at a single value for the whole sweep (so cache residency
is constant along the density axis) — confirm the campaign's chosen universe
size and state it in the caption; do not let it silently change between
re-runs, since that would move every crossover density reported in prose
elsewhere in the paper.

### Draft caption

> **Fig. 2 | The cost of intersection is U-shaped in density.** **a**, Cost
> per pair against density, both sides at equal density, universe pinned at
> [UNIVERSE] bits so cache residency is constant along the axis (Apple M4,
> L2-resident). One line per representation-pairing cell (colour/marker key,
> Methods); dotted grey line is the fixed-cost dense-bitmap kernel run
> without a zone map. Shaded bands mark the density ranges in which a
> compressed pairing beats the fixed-cost kernel (green) and in which it does
> not (grey); vertical lines mark the measured crossover densities. **b**,
> Speedup of each cell over the fixed-cost dense kernel from **a**, log
> scale; dashed line at 1.0× (no benefit). **c**, The winning kernel variant
> for each cell at each density — the map a representation selector must
> reproduce. Each point is the fastest of [N] repeats of [PAIRS] pairs
> sampled per density; oracle-checked, incorrect variants excluded.

### Construction route

matplotlib. **Extend, do not replace,** `bench/plot_density.py` — it already
implements exactly this three-panel structure against the exact schema
above, including the crossover-detection and band-shading logic. Confirm it
still runs against the re-generated `results/density.jsonl` once the
campaign completes; do not fork a second script. The one change needed: the
existing script separates "best B×B" from "fixed B×B" already (see its
`load()` docstring) — keep that distinction, it is exactly right and is the
mechanism that keeps A2's "flat" claim honest.

---

## 4. Fig. 3 — Vectorization wins only where work is irreducible

**Claim carried:** as working-set size grows past L2, SIMD-throughput
advantage decays toward parity on every microarchitecture measured, while
work-avoidance advantage holds or grows — and, stated once as an annotation
rather than a fourth panel, a SIMD-throughput kernel never wins any of the
nine asymmetric cells regardless of footprint.

### Panels

- **(a) SIMD advantage vs. working-set size.** X: corpus footprint in MB
  (log), spanning L1/L2/SLC/DRAM residency — mark the cache-level boundaries
  as light vertical guide lines, labelled (L2, SLC, DRAM) directly on the
  plot per house style (avoid a separate legend for this). Y: SIMD
  speedup over scalar, on the **B×B cell only** (log, reference line at
  1.0×). One line per host/ISA (Apple M4 NEON, and the two additional hosts
  — see naming correction below), house marks distinguishing host by
  linestyle since colour is reserved for representation identity, not host —
  **do not reuse the `B/S/R/W/C` palette for host identity**; use a
  qualitative host palette instead (`#66CCEE`, `#EE6677`, `#BBBBBB` — the two
  reserved slots from §1.1 plus grey), each host also direct-labelled at its
  rightmost point. Direct annotation block: *"SIMD applies only to B×B; the
  other nine cells never have a vectorized winner (0 of 25 measured
  corpus-cell points, Methods)."*
- **(b) Work-avoidance advantage vs. working-set size.** Same x-axis, same
  host colouring. Y: zone-map-planned B×B speedup over scalar B×B (log),
  reference at 1.0×. This is deliberately the *same cell* (B×B) under a
  *different strategy* (work avoidance via zone map, not wider SIMD lanes)
  so panels (a) and (b) are a true minimal pair — everything held constant
  except the mechanism.

### Data schema — checklist

Two distinct artifacts, and this is the item where the existing generator
(`bench/plot_paper.py`, function producing its "F4"/"F17" panel) currently
**hand-transcribes** a half-dozen numbers as Python literals rather than
reading a file. That was defensible once as a stopgap; it is not defensible
for the number that must survive a four-host re-run. **Before drafting this
figure, replace the hard-coded `F4` list with a real artifact:**

**New artifact needed: `results/residency.jsonl`**, one record per
`(host, footprint_mb, cache_level, strategy, ns_per_pair)`:

| Field | Meaning |
|---|---|
| `host` | one of the four campaign host tags (see naming correction below) |
| `footprint_mb` | corpus footprint actually realised (rows × universe/8), the x-axis |
| `cache_level` | `L1`/`L2`/`SLC`/`DRAM` — whichever the footprint lands in on that host; hosts have different cache sizes so this is **not** the same footprint→level mapping across rows, state that in Methods |
| `strategy` | `scalar` / `simd` / `zonemap` |
| `ns_pair` | measured cost |
| `corpus_shape` | must be held fixed (clustered/1-over-i, per the existing F4 comment) across the whole sweep — record it so a reviewer can verify |

Generate with a new script, `bench/residency_sweep.sh` (does not exist yet;
write it), driving `bench_cells.cpp --json` across a footprint sweep
(vary `--universe`/`--rows` at fixed density and structure) on all four
hosts, appending to one file per host under `results/hosts/*_residency.jsonl`
and concatenating. This mirrors `density_sweep.sh`'s pattern exactly — reuse
its shape, do not invent a new one.

**Host-naming correction — fix before use.** `bench/plot_paper.py`'s
`HOSTS` dict currently labels two hosts `'Neoverse (SVE)'` and `'Neoverse
(SVE2)'`. `NARRATIVE.md`'s Excluded section is explicit: *"the
Graviton3- and Graviton4-class hosts run the generic NEON path... there is no
SVE/SVE2-specific intrinsic code."* Labelling a plot axis "(SVE)" makes the
exact claim the paper disavows. Relabel to `"Neoverse V1 (NEON path)"` /
`"Neoverse N2 (NEON path)"` (or whatever the two ARM hosts actually are —
confirm against `RESEARCH_PLAN.md`'s host table) everywhere this dict is
used, including any figure inherited from the old `plot_paper.py` panel.

**A1's "0 of 25" annotation — its own small backing table**, so the caption
sentence is traceable to something more than prose: a 5-cell × 5-corpus-shape
grid (`results/OPTLOG.md`'s own structure) recording, per cell, whether the
winning variant at each shape is a SIMD-intrinsic kernel or not. This does
not need a new panel — it needs the counting artifact to exist as
`results/asymmetric_winners.csv` with columns `cell, corpus_shape, host,
winning_variant, is_simd` so the "0/25" (or whatever count survives
re-measurement, per the claim-stability table in `NARRATIVE.md` §4) is a
`grep`/`sum` away, not a hand count.

### Draft caption

> **Fig. 3 | Vectorization's advantage decays with working-set size; work
> avoidance does not.** **a**, Speedup of a SIMD dense-bitmap (B×B) kernel
> over its scalar equivalent, against corpus footprint, on [N] hosts spanning
> [ISA list]. Vertical guides mark the boundary between L2-, last-level-
> cache- and DRAM-resident footprints on [reference host]; footprint-to-cache
> mapping differs by host (Methods). Dashed line at 1.0× (no SIMD benefit).
> Across the nine asymmetric representation-pairing cells, a SIMD-throughput
> kernel wins zero of [N] measured corpus-cell points (Methods); B×B is the
> only cell where vectorization ever applies. **b**, Speedup of a zone-map–
> planned B×B kernel over the same scalar baseline, same footprint axis and
> hosts as **a**. Corpus shape held fixed at [clustered, 1/i spectrum] across
> both panels.

### Construction route

matplotlib, new script `paper/figures/gen/fig3_decay.py` reading
`results/residency.jsonl` (host, footprint, cache_level, strategy, ns_pair)
— replace, not extend, the hard-coded block in `bench/plot_paper.py`; keep
that script's `parse_f5`-style CSV/txt parsing pattern as a model for reading
per-host files, since it already solves "one file per host, concatenate."

---

## 5. Fig. 4 — Selection cost, regret, and the necessity of gating

**Claim carried:** per-pair selection is too expensive to pay (28–48%
region), hoisting to tile or probe-and-commit brings it under a 2% budget,
measured selection beats a modelled one, and — the panel this figure gains
under the revised inventory (§0, Change 3) — a work-avoidance mechanism
applied unconditionally is itself a net loss, so it must be gated exactly
the same way representation is selected.

### Panels

- **(a) Selection cost as % of runtime.** X: policy (categorical:
  per-pair / per-tile / probe-and-commit), grouped/faceted by host (3
  hosts side by side per policy — grouped bar, not stacked). Y: % of total
  runtime spent selecting (log, since per-pair costs ~30× more than the
  hoisted policies — a linear axis would flatten the hoisted bars to
  invisible). Horizontal reference line at 2% (the budget, per
  `PROBLEM_STATEMENT.md` §6/P2), hatched fill on any bar that fails it (per
  `paper-figures` house rule: hatch anything not a deployable result).
- **(b) Regret against the bucket oracle.** X: policy (all-bitmap /
  per-pair-model / per-tile / probe-and-commit — four bars), Y: % regret
  over the bucket oracle (linear; regret values span roughly 0–400% per
  A6's placeholder shape, so linear is legible and a log axis would
  compress the interesting middle). All-bitmap and the failed per-pair model
  are shown for contrast even though only per-tile/probe-and-commit are
  candidates for deployment — hatch those two to mark "not a candidate,"
  same convention as (a).
- **(c) Work avoidance must be gated (A9).** X: corpus (categorical, sorted
  by the always-filter speedup, one tick per corpus — reuse Table 1's
  corpus set so the two items cross-reference cleanly), Y: speedup over the
  unfiltered kernel (log), two series: "always filter" (grey, `#BBBBBB`,
  since this is the *not-adopted* policy) and "gated" (the paper's own
  colour, `#228833` green — reuse `R`'s hue loosely as "the work-avoidance
  mechanism," or introduce a sixth semantic colour if that reuse reads as
  confusing in a draft pass — decide once and hold it). Reference line at
  1.0×; annotate the count of corpora below 1.0× for each series directly on
  the plot ("11/25 harmed" / "0/25 harmed") rather than relying on the
  caption alone, per the direct-labeling house rule.

Panels (a)/(b) share a policy-axis vocabulary; panel (c) has an unrelated
x-axis (corpus, not policy) — this is acceptable for a Nature-style
multi-panel figure where each panel is independently legible, but the
caption must make the axis change explicit rather than implying continuity.

### Data schema — checklist

Three separate artifacts, three separate benchmark binaries — do not conflate
them:

| Panel | Binary / script | Needs (columns) | Granularity |
|---|---|---|---|
| (a) cost | `bench/bench_select.cpp` or `bench/gate_eval.sh` wrapper | `host, policy, sel_ns_per_pair, model_ns_per_pair, sel_pct_of_runtime, gate_pass(bool)` | one row per (host, policy) |
| (b) regret | `bench/bench_regret.cpp` | `host, policy, ns_per_pair, regret_pct, oracle_ns_per_pair, n_nonempty_buckets` — **label as bucket-oracle regret, a measured lower bound, not per-pair regret** (per `NARRATIVE.md` L1/A6: "regret is a lower bound... report as such") | one row per (host, policy) |
| (c) gating | `bench/filter_ablation.sh` → `results/ablation/filter.csv` | exact existing columns: `corpus, universe, rows, mean_card, density, disjoint_pct, no_filter_ns, filter_ns, gated_ns, filter_speedup, gated_speedup, gate, correct` | one row per corpus |

Panels (a)/(b) currently exist only as **stdout text** from
`bench_select`/`bench_regret`/`gate_eval.sh` on one host at a time — there is
no retained structured artifact. **Before drafting this figure, add a
`--csv PATH` (or reuse the `--json` convention from `bench_cells.cpp`) output
mode to `bench_select.cpp` and `bench_regret.cpp`**, or wrap their stdout
with a small parser analogous to `bench/plot_paper.py`'s `parse_f5()` regex
approach, and retain the per-host raw files under
`results/hosts/*_select.{txt,csv}` the same way the density sweep retains
`density.jsonl`. Panel (c)'s CSV already exists in the right shape — no new
instrumentation needed there, only a re-run.

### Draft caption

> **Fig. 4 | Selection is cheap only when hoisted, and work avoidance must
> itself be gated.** **a**, Selection cost as a percentage of total runtime
> for three policies — per-pair, per-tile, and probe-and-commit — on [N]
> hosts. Dashed line at the 2% budget (Methods); hatched bars fail it. **b**,
> Regret of each policy against a bucket oracle that times every candidate
> cell in bulk over pairs grouped by shape and keeps the per-bucket minimum —
> an unattainable, non-deployable denominator, and a measured lower bound on
> true per-pair regret (Methods). Hatched bars (all-bitmap; per-pair model)
> are shown for scale, not as deployment candidates. **c**, Speedup of the
> zone-map work-avoidance filter over the unfiltered kernel across [N] real
> and synthetic corpora (Table 1 union; sorted by unconditional-filter
> speedup), applied unconditionally (grey) versus gated by the same selection
> machinery as **a** (green). Dashed line at 1.0×; annotated counts give the
> number of corpora harmed (speedup below 1.0×) under each policy.

### Construction route

matplotlib, one script `paper/figures/gen/fig4_selection.py`, three
subplot axes. Panel (c) can be lifted almost directly from a straightforward
`pandas.read_csv('results/ablation/filter.csv')` — no new parsing logic
needed. Panels (a)/(b) need the instrumentation change above before the
script can be written against real columns rather than re-parsing free-form
stdout.

---

## 6. Table 1 — Real corpora: the winning cell migrates

**Claim carried:** across seventeen real corpora spanning eight orders of
magnitude of density, Storm's best cell beats a `run_optimize`-tuned
CRoaring on every one, and — the more important half of the claim — no
single cell wins everywhere; the winner tracks density.

### Structure

`booktabs`, row-grouped by density band via a rotated left rail
(`\multirow{n}{*}{\rotatebox{90}{...}}`) rather than repeating a "band" column
seventeen times — three groups: **sparse** (density below the measured
crossover), **mixed** (B×B's correct-winner band), **dense** (above the
upper crossover / near-symmetric-complement band). This directly visualises
A4 (the winner migrates) as you read down the rail, which is the entire
point of the table per `NARRATIVE.md` §6 ("Table 1 is the paper... more than
any ratio in it").

**On L6 (precision) — this changes the column design, not just the
caption.** `NARRATIVE.md` L6 is explicit that per-corpus ratios are not
trustworthy to two significant figures under the current single-batch
protocol, that the winning cell itself changed on 5/17 corpora between
repeats, and that `uscensus2000` and `dimension_033` are unquotable until the
harness is fixed. **The campaign must retain per-repeat data, not just a
single min-of-N, before this table can be drawn**: `bench/run_corpora.sh`
currently calls `bench_baseline` once per corpus with `--repeats 7`
internally-averaged; the table needs the **distribution across repeats**,
ideally round-robin-interleaved across corpora (per the method that fixed the
same problem in `bench/bench_bloom.cpp`, §15.11) rather than run back-to-back
per corpus, to remove the thermal/cache-pollution drift `NARRATIVE.md` L6
names. Report **median [IQR]**, not a point ratio.

### Columns

| Column | Format (siunitx) | Notes |
|---|---|---|
| Corpus | `l` | e.g. `census1881`, with `\_srt` variants immediately below their unsorted counterpart (paired rows, `\addlinespace` between pairs not corpora) |
| Universe $m$ | `S[table-format=1.2e1]` (scientific) | bits |
| Density | `S[table-format=1.2e-1]` | fraction, both `d` |
| Winning cell | `l`, **bold** | the single most load-bearing cell in the table — never truncate or abbreviate cryptically; use the same two-symbol notation as Fig. 2/4 (`B×S` etc.) |
| vs. CRoaring (`run_optimize`) | `S[table-format=2.1]` + `×`, **primary bolded column** | median across repeats; `[IQR]` in a `{\tiny(...)}` suffix per the secondary-annotation convention, not a separate column |
| vs. all-bitmap | `S[table-format=4.0]` + `×`, plain (not bold) | context only, small font — per `NARRATIVE.md` Reconciliation #3, the CRoaring column is the honest headline; do not let this column visually compete with it |

`uscensus2000` and `dimension_033`: keep the row (density and winning cell
are still informative), replace the ratio cells with a dagger `†` and a
caption note, per L6's instruction to drop them from any headline without
deleting them from the record.

**Row count.** Seventeen corpora (plus paired `_srt` variants already counted
within that seventeen, per `RESEARCH_PLAN.md` §15.1's twelve-plus-`_srt`
scheme) is within the "too many" line only if set tight:
`\footnotesize`, `\setlength{\tabcolsep}{4pt}`, three row-groups with a
rotated rail. If it still overflows the text block after that, the fallback
is **not** to drop corpora from main text (`NARRATIVE.md` calls the full set
"the paper") — split instead into a 3-column-narrower main table (corpus,
density, winning cell, vs. CRoaring only) with the "vs. all-bitmap" and
per-repeat IQR detail moved to a full-width Supplementary version (S-item,
§8). Try the full six-column version first.

### Draft caption

> **Table 1 | The winning representation pairing migrates with corpus
> density.** Seventeen real corpora (`RESEARCH_PLAN.md` §15.1; the twelve
> `real-roaring-datasets` corpora CRoaring's own harness runs by default,
> `_srt` row-sorted variants, and four larger modern graphs), grouped by
> density band. "vs. CRoaring" reports Storm's best cell (oracle selection,
> not the shipped selector; selection cost is reported separately, Fig. 4)
> against `run_optimize()`-tuned CRoaring, median [interquartile range]
> across [N] round-robin-interleaved repeats. "vs. all-bitmap" is reported
> for scale only. † `uscensus2000` and `dimension_033` are excluded from
> quoted ratios pending a measurement-drift fix (Methods); density and
> winning cell are unaffected. Winning cell in bold.

### Construction route

`booktabs` + `siunitx`, hand-written LaTeX generated from
`bench/summarize_corpora.py`'s existing markdown output — extend that script
to emit a LaTeX `tabular` body (a `--tex` flag) directly from
`results/corpora/*.txt`, rather than hand-transcribing seventeen rows, which
is exactly the kind of manual-number risk `paper-figures`' provenance rule
exists to prevent. The regex parser in `summarize_corpora.py` already reads
every field this table needs except the per-repeat distribution — extending
it to read the interleaved-repeat log format (once that logging exists,
prerequisite above) is a smaller change than rewriting the parser.

---

## 7. Table 2 — Roaring's fixed-chunk threshold misprices dense corpora (C11)

**New table, promoted per §0 Change 1.** **Claim carried:** Roaring's
per-2¹⁶-chunk cardinality test against a fixed 4096 threshold is evaluated on
the wrong statistic — local chunk mass, not global density — so a corpus can
be 6–17% dense globally and still receive almost no bitset containers,
because that density is spread across enough chunks that none individually
crosses the threshold.

### Structure

Small `booktabs` table, 5–8 rows (only the corpora where the phenomenon is
visible — the dense/mixed-band corpora from Table 1, not all seventeen; a
sparse corpus like `uscensus2000` has few containers of any kind and adds
nothing here). No row-grouping needed at this size — flat structure, no
rail.

### Columns

| Column | Format | Notes |
|---|---|---|
| Corpus | `l` | subset of Table 1's names — reuse identically so the two tables are visibly the same experiment |
| Global density | `S[table-format=1.1e-1]` | |
| Array containers | `S[table-format=5.0]`, **bold if plurality** | |
| Bitset containers | `S[table-format=5.0]`, **bold if plurality** | the column the whole claim is about — `census1881` should read **0** here |
| Run containers | `S[table-format=5.0]`, **bold if plurality** | |
| Total chunks | `S[table-format=5.0]` | for context — lets a reader sanity-check array+bitset+run against the chunk count |

Bold marks whichever container type is the plurality for that row, which is
what makes the mismatch (dense corpus, plurality = array) visually pop
without needing an extra annotation column.

### Data schema — checklist, and a new instrumentation requirement

**This data does not currently come out of any checked-in benchmark
binary.** `RESEARCH_PLAN.md` §15.3's table was produced by an ad hoc call to
CRoaring's own introspection API and was not retained as a script. Before
this table can be drawn, add container-census output to the corpus pipeline:

| Field | Source |
|---|---|
| `corpus` | existing corpus registry (`bench/run_corpora.sh`) |
| `global_density` | already computed per corpus |
| `n_array_containers` | `roaring_statistics_t.n_array_containers`, from `roaring_bitmap_statistics()` in `third_party/croaring_amalg/roaring.h` (confirmed field name, CRoaring v4.7.2, vendored) |
| `n_bitset_containers` | `roaring_statistics_t.n_bitset_containers` |
| `n_run_containers` | `roaring_statistics_t.n_run_containers` |
| `n_containers` (total) | sum of the above, or read directly from the same struct |

Call `roaring_bitmap_statistics()` **after** `run_optimize()` — the whole
point is what containers CRoaring settles on once it has had the chance to
pick run containers, matching how it is actually used and matching the
`vs. CRoaring(run_optimize)` comparison in Table 1. Add this as a
`--container-census` flag to `bench/run_corpora.sh` (it already builds and
runs a CRoaring bitmap per corpus for the timing comparison; the statistics
call is three lines against a bitmap already in hand) writing
`results/corpora/container_census.csv` with exactly the six fields above.
Tag this evidence **tier 3** for the measured counts and **tier 2** for the
threshold value itself (4096, per CRoaring source — cite as a vendor fact,
not a measurement).

### Draft caption

> **Table 2 | Roaring's fixed per-chunk threshold is tested against the
> wrong statistic.** Container composition of a `run_optimize()`-tuned
> CRoaring bitmap for corpora spanning [density range], from
> `roaring_bitmap_statistics()` (CRoaring v4.7.2). Roaring selects a bitset
> container only when a single 2¹⁶-bit chunk's cardinality exceeds 4096;
> with a large universe, global density can reach [X]% while every
> individual chunk stays under threshold, so array containers dominate a
> nominally dense corpus. Bold marks the plurality container type per row.

### Construction route

`booktabs` + `siunitx`, generated from `results/corpora/container_census.csv`
by a small script analogous to `summarize_corpora.py --tex` (§6) — same
pattern, do not invent a third table-generation approach for this paper.

---

## 8. Table typography — house rules for both tables

- **Rules.** `\toprule`/`\bottomrule` heavy (0.08 em), `\midrule` between
  row-groups light (0.05 em), `\cmidrule(lr){i-j}` for any spanning header
  or subtotal. Never `\hline`. Never a rule between individual data rows —
  use `\addlinespace` for breathing room within a group (Table 1's `_srt`
  pairs).
- **Vertical rules** only at the row-group rail boundary in Table 1
  (`@{}c l | c c c c@{}`-style, rail column then a `|` then the data
  columns) — not one per column, not in Table 2 (too small to need a rail).
- **Bold/underline convention**, stated verbatim in every caption that uses
  it: "Winning cell in bold" (Table 1); "Bold marks the plurality container
  type per row" (Table 2). No underline convention is needed in either
  table — reserve underline for a future table that needs a genuine
  best/second-best contrast, which neither of these is.
- **Missing/excluded token**: `†` with a caption footnote (Table 1's two
  excluded corpora) — do not also introduce `-`/`--` for the same meaning
  in the same table.
- **Density.** `\footnotesize`, `\setlength{\tabcolsep}{4pt}` for Table 1
  (six columns, seventeen-ish rows), `\setlength{\tabcolsep}{6pt}` for
  Table 2 (six columns, ~7 rows — can afford to be slightly airier). Neither
  should need `\resizebox`; if Table 1 does after tightening, drop the
  "vs. all-bitmap" column to Supplementary before resizing type down.
- **Caption above table, `\label` immediately after `\caption`.** No note
  block below `\bottomrule` — everything decodable goes in the caption
  itself, per the house convention.
- **Split-to-Supplementary trigger**: if either table's row count grows
  materially (a fourth container type, more than ~20 corpora), move the full
  version to Supplementary and keep an abbreviated (8–10 row, span-the-range)
  version in main text — do not let a main table grow past roughly one
  column-page of `\footnotesize` type.

---

## 9. Supplementary display inventory

Numbered so every item `NARRATIVE.md`'s own Supplementary paragraph lists is
homed, plus the storage table demoted from main (§0 Change 2). Each gets a
one-line claim; full specs are not written here (out of scope for a
six-main-item audit) but the schema each needs is already implied by the
RESEARCH_PLAN.md section cited.

| # | Item | One-line claim | Source |
|---|---|---|---|
| **S1** | Storage: bits/value per representation, metadata reported separately | A dense bitmap over a large sparse universe is a storage liability, not a compression scheme; selector metadata (rank index + zone map) is itself universe-proportional and, on the worst corpus, ~26,000× the data it describes | `bench/compression.cpp` CSV: `corpus,rows,elements,universe,bitmap_bpv,array_bpv,runs_bpv,ewah_bpv,best_bpv,meta_bpv,meta_pct_of_best`; RESEARCH_PLAN.md §24 |
| **S2** | Per-cell optimization record | ~180 kernel variants raced across 13 rounds; this is the "how we found the winners" record behind every bold cell in Table 1/Fig. 2 | `results/OPTLOG.md` (regenerate) |
| **S3** | Four-host synthetic CRoaring comparison (B1) | Cross-ISA advantage on synthetic skewed data, 1.7–19× range, peak on Sapphire Rapids — kept as a secondary result once the real-corpus figure (Table 1) exists as the primary one | `bench/sweep_threshold.sh` / per-host dsweep logs |
| **S4** | B×R run-length isolation (A7, claim P4) | A rank index makes B×R cost depend on run *count*, not run *length* — run length varied 256× at fixed count | `bench/p4_runlength.sh` |
| **S5** | Zone-map gating ablation, full 25-corpus detail | The full per-corpus table behind Fig. 4c's summary — every corpus's unfiltered/filtered/gated triple, not just the aggregate geomean/worst/best | `results/ablation/filter.csv` (same file Fig. 4c reads; S5 is its full-table rendering, not a new artifact) |
| **S6** | Complement cell (B5) | Computing on the complement above density ½ is a selectable, statable strategy — modest (~20%) win over the accidental R×R, presented honestly as "selectable," not "fast" | `results/` complement-cell (F13) benchmark, `cell_comp.cpp` |
| **S7** | Negative-result detail (B3, B4) | Harley-Seal, D2 run-collapsing, register blocking (refuted at L2 *and* DRAM), prefetch inversion, the port-pressure trace that refuted the port-bound hypothesis | `RESEARCH_PLAN.md` §12–13 negative-result log |
| **S8** | Correctness-testing record | `test_storm` 1,279 checks; `test_cells` differential-vs-oracle at 2.1M+ checks; 2.4–2.7M per host family in the cross-ISA campaign; `nm` ABI check (42 unmangled `STORM_` exports, zero mangled) | `tests/test_storm.c`, `tests/test_cells.*`, CI logs |
| **S9** | Per-corpus raw timings, all repeats | The full interleaved-repeat distribution behind Table 1's median[IQR] — lets a reviewer recompute the aggregation choice | `results/corpora/*.txt` raw, plus the round-robin repeat log once that instrumentation lands (§6) |
| **S10** | Disjointness / "emptiness-proving" workload characterization (C13) | 95–100% of pairs in the target regime are disjoint; the operation being optimized is disjointness-proof, not intersection-count — reframes why the headline ratios are so large | `bench_baseline`'s `# DISJOINT` output, per corpus |

---

## 10. Abstract-to-display traceability (re-audit after the C11/storage swap)

`NARRATIVE.md` §6's abstract audit maps four clauses to display items; the
inventory change in §0 shifts one of them. Re-stated:

- "wins zero of 25" → Fig. 3a (annotation, backed by `results/asymmetric_winners.csv`, §4)
- "17 of 17 corpora" → Table 1
- "0.1–[X]% selection cost" → Fig. 4a
- "U-shaped" → Fig. 2
- *(new, if the abstract claims anything about Roaring's mechanism specifically, per NARRATIVE.md §3's framing move)* → Table 2 (C11)
- Any storage clause is now backed by **Supplementary S1, not a main item** —
  per `NARRATIVE.md`'s own instruction ("do not let a clause about storage...
  into the abstract unless Table 2 or Table 1 substantiates it directly"),
  this means **a storage clause should no longer appear in the abstract** at
  all under the revised inventory, since Table 2 no longer carries storage.
  Flag this explicitly to whoever drafts the abstract.
