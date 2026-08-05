# Real-corpus benchmark: 1:1 overlap with the Roaring papers, plus larger modern data

Formal run record. Reproduce with `bench/run_corpora.sh`, collate with
`bench/summarize_corpora.py`. Raw per-corpus output is in `results/corpora/`.

## 1. Why this run exists

Every Storm-vs-CRoaring number before this was measured on **our own generator**
(`kernels/storm_gen.cpp`). A generator can encode the very structure the kernels
exploit, so those numbers could not falsify the claim — and "you chose
favourable data" is the first objection a reviewer raises.

The twelve `real-roaring-datasets` corpora are the files **CRoaring's own
benchmark harness runs by default** (its README names `census1881` as the
default pick). Running on the incumbent's chosen data, with `run_optimize()`
applied so it gets its run containers, removes that objection. The larger graphs
then extend the map rather than constituting it.

Loader validation: `tools/sets2bin.py` reproduces census1881's published
statistics exactly — universe 4,277,806, mean |Xi| 5,019.3 — matching
Lemire et al., arXiv:1603.06549.

## 2. Method

| | |
|---|---|
| host | Apple M4, macOS (`arm64-Darwin`), single core |
| build | `-O3 -std=c++17 -mcpu=native`; CRoaring v4.7.2 amalgamation, `-O3 -std=c11 -mcpu=native` |
| baseline | `roaring_bitmap_and_cardinality`, both as-built and after `roaring_bitmap_run_optimize()`; **all ratios quoted against the run-optimized variant**, which is the faster of the two on 16 of 17 corpora |
| rows | ≤256 per corpus, stride-sampled across the corpus |
| pairs | ≤20,000, walked as a **strided traverse of the linearised upper triangle**, not a truncated row-major fill |
| timing | best of 7 repeats over the whole pair batch |
| correctness | every kernel checked against `roaring_bitmap_and_cardinality` on every pair **before** any timing; a mismatch aborts the run |

The pair-sampling detail is not cosmetic. Filling a 20k cap in row-major order
stops at `i≈40` for a 256-row corpus, so every pair's dense side would come from
the first few rows — and row order is never arbitrary (real-roaring files are
attribute-ordered; graph vertex ids follow crawl/BFS order). That exact mistake
already invalidated an earlier optimisation campaign in this project, making
every absolute number 2.4x too optimistic. It is fixed in
`bench/bench_real.cpp` and `bench/bench_baseline.cpp`.

## 3. Headline

**17 of 17 corpora: Storm's best cell beats tuned CRoaring.** Range 1.12x to 16.06x, median ≈ 5.4x.

Ordered by density, because density is the paper's independent variable.

| corpus | universe m | sets | mean \|Xi\| | density | best cell | ns/pair | vs CRoaring_ro | vs all-bitmap |
|---|---:|---:|---:|---:|---|---:|---:|---:|
| uscensus2000 | 36,974,578 | 200 | 30 | 8.09e-07 | B x S | 7.5 | **2.29x** | 24,919x |
| com-LiveJournal | 4,036,538 | 256 | 21 | 5.14e-06 | B x B | 28.8 | **2.04x** | 544x |
| as-skitter | 1,696,415 | 256 | 15 | 9.08e-06 | B x S | 7.2 | **4.02x** | 756x |
| soc-Pokec | 1,632,804 | 256 | 25 | 1.55e-05 | B x B | 27.1 | **4.36x** | 175x |
| wiki-Talk | 2,394,385 | 256 | 52 | 2.17e-05 | B x S | 9.2 | **8.80x** | 910x |
| dimension_003 | 3,866,847 | 256 | 91 | 2.36e-05 | S x S | 2.8 | **1.12x** | 4,042x |
| com-Orkut | 3,072,627 | 256 | 82 | 2.66e-05 | B x B | 39.7 | **6.07x** | 232x |
| dimension_008 | 3,866,845 | 256 | 347 | 8.98e-05 | R x R | 3.9 | **1.73x** | 2,846x |
| census1881_srt | 4,277,735 | 200 | 3,404 | 7.96e-04 | B x R | 30.4 | **2.33x** | 387x |
| wikileaks-noquotes | 1,353,179 | 200 | 1,377 | 1.02e-03 | B x B | 60.8 | **6.79x** | 67x |
| wikileaks-noquotes_srt | 1,353,133 | 200 | 1,440 | 1.06e-03 | B x B | 19.8 | **6.06x** | 223x |
| census1881 | 4,277,806 | 200 | 5,019 | 1.17e-03 | B x B | 79.5 | **16.06x** | 151x |
| dimension_033 | 3,866,847 | 173 | 22,352 | 5.78e-03 | B x R | 36.7 | **2.82x** | 290x |
| weather_sept_85 | 1,015,367 | 200 | 64,353 | 6.34e-02 | B x B | 1178.8 | **15.02x** | 2x |
| weather_sept_85_srt | 1,015,367 | 200 | 80,540 | 7.93e-02 | B x B | 322.7 | **5.44x** | 8x |
| census-income_srt | 199,523 | 200 | 30,464 | 1.53e-01 | B x B | 136.0 | **9.78x** | 3x |
| census-income | 199,523 | 200 | 34,610 | 1.73e-01 | B x B | 301.9 | **13.11x** | 1x |

The `_srt` rows are the row-sorted variants the Roaring papers report
separately; sorting raises clustering, giving two clustering points per corpus.

## 3a. Run-to-run stability — two independent sweeps

The whole sweep was run twice. Both runs beat tuned CRoaring on **17/17**, so
the direction is robust. Nothing else about the numbers is.

| | |
|---|---|
| corpora where Storm wins | **17/17 in both runs** |
| ratio drift between runs | median **1.15x**, max **1.65x** (uscensus2000 1.39x -> 2.29x) |
| winning cell changed | **5/17** — uscensus2000, com-LiveJournal, census1881_srt, census1881, dimension_033 |

Three of the five winner flips are between near-tied cells and are harmless: on
census1881 run 1 had B x B at 15.81x and B x S at 15.83x, so which one "wins" is
a coin flip and the corpus-level ratio moved only 1.01x. A selector picking
either is equally right, which is the property you want.

The other two are not harmless. `uscensus2000` (B x R -> B x S, ratio 1.65x) and
`dimension_033` (B x B -> B x R, 1.49x) move both the winner and the magnitude.
Those two rows should not be quoted at all until the timing harness is fixed.

**What is safe to claim from this data:** Storm's best cell beats tuned CRoaring
on every corpus tested, across five orders of density; the winner is
density-dependent and no single pairing dominates. **What is not safe:** any
specific ratio to two significant figures, and the identity of the winning cell
on the five unstable corpora.


## 4. The actual result: no single cell wins

Winning-cell distribution across 17 corpora: **B x B x10, B x S x3, B x R x2,
S x S x1, R x R x1.**

This matters more than any single ratio. If one column dominated, the pairing
matrix would be unnecessary and the paper would refute itself. Full per-cell
speedups over tuned CRoaring are in `results/corpora/SUMMARY.md`; the winner
migrates from B x R / S x S / B x S at the sparse end to B x B at the dense end,
and the columns that lose, lose badly (W x W is below 1.0x on 16 of 17).

The `vs all-bitmap` column spans **1x to 23,080x** over five orders of density.
That column *is* the density map: it collapses to 1x on `census-income`
(universe 199,523, density 0.17 — all-bitmap is already correct there) and
reaches 23,080x on `uscensus2000` (universe 3.7e7, density 8.1e-7).

## 5. Why Storm wins on the *dense* corpora — a sourced mechanism

The dense-end wins (weather_sept_85 10.75x, census-income 12.82x) look wrong at
first: at 6–17% density both sides should be doing near-identical popcount work.
They are not. Container census via `roaring_bitmap_statistics()` after
`run_optimize()`, 200 rows each:

| corpus | array | bitset | run | global density |
|---|---:|---:|---:|---:|
| weather_sept_85 | **2,274** | 561 | 21 | 6.3e-02 |
| census-income | **553** | 180 | 35 | 1.7e-01 |
| census1881 | **1,332** | 0 | 132 | 1.2e-03 |
| wikileaks-noquotes | 199 | 0 | **1,693** | 1.0e-03 |
| as-skitter | 83 | 0 | 117 | 9.1e-06 |
| uscensus2000 | **2,219** | 0 | 2 | 8.1e-07 |

Roaring chooses its container by testing **per-2^16-chunk cardinality against a
fixed threshold of 4096**. With a large universe the set mass spreads thinly
enough that individual chunks stay under that threshold even when *global*
density is 6% or 17% — so CRoaring runs array-merge intersections where Storm
popcounts a bitmap. census1881 has **zero** bitset containers at density 1.2e-3.

This is the fixed-chunk / fixed-threshold weakness the pairing-matrix argument
targets, observed directly rather than inferred: the container decision is made
on the wrong statistic (per-chunk cardinality) for corpora whose structure is
set by the *global* universe. It is also why the win is not a SIMD story —
it is a "do the right kind of work" story, consistent with this project's
standing finding that only dense-x-dense wins by vectorising.

## 6. Honest limitations

1. **Single microarchitecture.** Apple M4 only. The four-ISA sweep (M4,
   Neoverse SVE `fpga-neo1`, Neoverse SVE2 `fpga-neo2`, Sapphire Rapids
   `fpga-sapphire`) has not been rerun on these corpora. Prior work found
   zone-map *growth* is ARM-only and flat on Sapphire, so the x86 numbers will
   differ and must be measured, not extrapolated.
2. **The two marginal wins are real wins — but the variance is worse than the
   margins.** `dimension_003` (1.09x) and `uscensus2000` (1.39x) were initially
   called ties on the assumption that noise might have pushed them above 1.0.
   Repeating them as independent processes refutes that: dimension_003 gives
   1.21, 1.26, 1.28, 1.36, 1.37, 1.39, 1.44, 3.72 and uscensus2000 gives 2.05,
   2.24, 2.98, 3.29 -- every repeat is *above* the recorded value. Both are
   wins. **17/17 stands.**
3. **But no ratio here is good to two significant figures.** Independent repeats
   span 16.1-18.2x (census1881), 7.6-13.9x (census-income) and 1.2-3.7x
   (dimension_003). The fastest corpora are the noisiest: at 2.9 ns/pair a
   20,000-pair batch lasts 58 us, short enough for scheduling and frequency ramp
   to dominate. Worse, the bias is **systematic** -- the table was collected as
   one back-to-back batch of 17 corpora and standalone repeats land at or above
   the recorded figure in every case checked, implicating thermal accumulation
   and cross-corpus cache pollution. The table understates Storm, but it is
   uncontrolled in both directions. Fix: interleaved round-robin across corpora
   plus median-and-IQR over N independent runs, as `bench_cells` already does
   per-variant.
4. **Row counts are small** (173–256 sets per corpus) and for the 200-set
   real-roaring corpora that is the entire corpus, but for the graphs it is a
   stride-sample of millions of vertices.
5. **Selection cost is excluded** from these numbers — each cell is timed
   directly. The near-free-selection claim (Gate 1, 0.10–0.48%) is measured
   separately in `bench_select`/`bench_regret` and has not been rerun here.
6. **`vs all-bitmap` is a weak baseline** at large universe: it costs
   `rows x m / 8` bytes (104 MB for census1881 at 200 rows) and is closer to a
   strawman than a competitor. The CRoaring column is the defensible one.

## 7. Zone-map ablation — is the side structure worth it?

The zone map (1 bit per 512-bit bin) is built **unconditionally** by
`build_row()`, so the main table can never answer "is it worth it?" — it only
ever shows the planned kernel. `bench_baseline` now times each cell's best
index-free variant against its index-using one, and reports the system-level
number: best cell when side structures are allowed, versus best cell when they
are not.

The index-free B×B baseline is `neon_u8`, not `dense`. Comparing a planned
kernel against the *portable* multi-accumulator would have inflated the
mechanism for the wrong reason on ARM.

| corpus | density | winning cell | B×B alone | B×S alone | B×R alone | best no-index | best indexed | **net gain** |
|---|---:|---|---:|---:|---:|---:|---:|---:|
| uscensus2000 | 8.1e-07 | B x S | 745.72x | 1.26x | 2.14x | 7.5 | 7.5 | **1.00x** |
| com-LiveJournal | 5.1e-06 | B x B | 440.91x | 2.63x | 6.48x | 31.0 | 28.8 | **1.07x** |
| as-skitter | 9.1e-06 | B x S | 397.26x | 0.53x | 1.37x | 7.2 | 7.2 | **1.00x** |
| soc-Pokec | 1.6e-05 | B x B | 168.67x | 2.42x | 2.65x | 41.0 | 27.1 | **1.51x** |
| wiki-Talk | 2.2e-05 | B x S | 178.35x | 0.31x | 1.23x | 9.2 | 9.2 | **1.00x** |
| dimension_003 | 2.4e-05 | S x S | 438.99x | 0.96x | 1.13x | 2.8 | 2.8 | **1.00x** |
| com-Orkut | 2.7e-05 | B x B | 234.48x | 2.16x | 5.58x | 118.1 | 39.7 | **2.98x** |
| dimension_008 | 9.0e-05 | R x R | 221.01x | 0.54x | 1.10x | 3.9 | 3.9 | **1.00x** |
| census1881_srt | 8.0e-04 | B x R | 361.28x | 0.91x | 3.59x | 37.1 | 30.4 | **1.22x** |
| wikileaks-noquotes | 1.0e-03 | B x B | 63.52x | 0.28x | 0.85x | 151.1 | 60.8 | **2.49x** |
| wikileaks-noquotes_srt | 1.1e-03 | B x B | 224.85x | 1.03x | 2.61x | 75.1 | 19.8 | **3.80x** |
| census1881 | 1.2e-03 | B x B | 140.45x | 0.25x | 0.85x | 84.7 | 79.5 | **1.06x** |
| dimension_033 | 5.8e-03 | B x R | 230.30x | 0.78x | 0.85x | 39.9 | 36.7 | **1.09x** |
| weather_sept_85 | 6.3e-02 | B x B | 2.21x | 0.24x | 0.78x | 2600.1 | 1178.8 | **2.21x** |
| weather_sept_85_srt | 7.9e-02 | B x B | 8.59x | 0.37x | 0.92x | 1124.5 | 322.7 | **3.48x** |
| census-income_srt | 1.5e-01 | B x B | 2.80x | 0.37x | 0.82x | 381.4 | 136.0 | **2.80x** |
| census-income | 1.7e-01 | B x B | 1.18x | 0.29x | 0.77x | 355.0 | 301.9 | **1.18x** |

Net: helps 12/17, neutral 5/17, hurts 0/17. Footprint 0.195% of bitmap bytes.

Read this by column, not by row.

**`B×B alone` is spectacular and mostly irrelevant.** 745×, 441×, 397× — but
those are corpora where B×B loses to a sparse cell regardless. The zone map is
rescuing a kernel that was never going to be selected. Quoting that column as
the zone-map benefit would be dishonest.

**`B×S alone` is mostly below 1.0** (0.24×–2.63×, below 1.0 on 10 of 17).
`bs_occ` generally loses to plain `ilp8`: when the sparse side is a short list,
gating each 512-bit bin costs more than the probes it saves. A clean negative
result for the zone map in the cell where it was most tempting.

**`net gain` is the honest number: 1.00×–3.80×, helping 12 of 17, neutral on 5,
hurting 0.** The five neutral corpora are exactly those where the winner is a
sparse cell (B×S, S×S, R×R) that reads no side structure — the zone map is
built, unused, and paid for only in space.

**It never loses at system level** (minimum 1.00×) precisely *because*
selection routes around it. Per-cell it can lose badly — B×S with zone map on
census1881 is 0.25× — so the mechanism is only safe in the presence of the
selector. That coupling is worth stating explicitly in the paper: the zone map
is not independently a good idea, it is a good idea *given* a selector that can
decline it.

**Cost: 0.195% of bitmap bytes**, and the build is one pass at construction.

The largest gains cluster at both ends of the density range — com-Orkut 2.98×
and wikileaks_srt 3.80× at the sparse end, weather_srt 3.48× and
census-income_srt 2.80× at the dense end — with the middle mostly neutral. The
`_srt` variants gain consistently more than their unsorted counterparts
(3.80 vs 2.49, 3.48 vs 2.21, 2.80 vs 1.18), which is the expected direction:
sorting raises clustering, and clustering is what makes whole bins empty.

## 9. Open items

- Rerun across all four ISAs.
- Repeat with error bars; downgrade the two marginal wins to ties if they do
  not survive.
- Add end-to-end numbers *including* selection cost, not just per-cell.
- `UShER SARS-CoV-2` (8.45M genomes, open, 678 MB variant-major VCF) is the one
  real *genomic* dataset that reaches the target regime; density unmeasured.
- msprime/stdpopsim for a principled scale sweep, replacing the hand-rolled
  1/i draw in `storm_gen.cpp`.
