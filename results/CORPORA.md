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

**17 of 17 corpora: Storm's best cell beats tuned CRoaring.** Range 1.09x to
15.83x, median ≈ 5.6x.

Ordered by density, because density is the paper's independent variable.

| corpus | universe m | sets | mean \|Xi\| | density | best cell | ns/pair | vs CRoaring_ro | vs all-bitmap |
|---|---:|---:|---:|---:|---|---:|---:|---:|
| uscensus2000 | 36,974,578 | 200 | 30 | 8.09e-07 | B x R | 8.9 | **1.39x** | 23,080x |
| com-LiveJournal | 4,036,538 | 256 | 21 | 5.14e-06 | B x S | 18.7 | **3.28x** | 572x |
| as-skitter | 1,696,415 | 256 | 15 | 9.08e-06 | B x S | 5.5 | **5.61x** | 1,180x |
| soc-Pokec | 1,632,804 | 256 | 25 | 1.55e-05 | B x B | 20.9 | **7.18x** | 201x |
| wiki-Talk | 2,394,385 | 256 | 52 | 2.17e-05 | B x S | 8.0 | **12.50x** | 1,507x |
| dimension_003 | 3,866,847 | 256 | 91 | 2.36e-05 | S x S | 2.9 | **1.09x** | 4,770x |
| com-Orkut | 3,072,627 | 256 | 82 | 2.66e-05 | B x B | 38.1 | **6.06x** | 211x |
| dimension_008 | 3,866,845 | 256 | 347 | 8.98e-05 | R x R | 3.9 | **1.83x** | 4,160x |
| census1881_srt | 4,277,735 | 200 | 3,404 | 7.96e-04 | B x B | 31.4 | **2.02x** | 355x |
| wikileaks-noquotes | 1,353,179 | 200 | 1,377 | 1.02e-03 | B x B | 57.8 | **7.42x** | 61x |
| wikileaks-noquotes_srt | 1,353,133 | 200 | 1,440 | 1.06e-03 | B x B | 19.0 | **5.33x** | 201x |
| census1881 | 4,277,806 | 200 | 5,019 | 1.17e-03 | B x S | 79.8 | **15.83x** | 140x |
| dimension_033 | 3,866,847 | 173 | 22,352 | 5.78e-03 | B x B | 51.7 | **1.89x** | 275x |
| weather_sept_85 | 1,015,367 | 200 | 64,353 | 6.34e-02 | B x B | 1823.0 | **10.75x** | 2x |
| weather_sept_85_srt | 1,015,367 | 200 | 80,540 | 7.93e-02 | B x B | 656.7 | **3.75x** | 8x |
| census-income_srt | 199,523 | 200 | 30,464 | 1.53e-01 | B x B | 134.9 | **10.59x** | 3x |
| census-income | 199,523 | 200 | 34,610 | 1.73e-01 | B x B | 298.2 | **12.82x** | 1x |

The `_srt` rows are the row-sorted variants the Roaring papers report
separately; sorting raises clustering, giving two clustering points per corpus.

## 4. The actual result: no single cell wins

Winning-cell distribution across 17 corpora: **B x B x10, B x S x4, B x R x1,
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

## 7. Open items

- Rerun across all four ISAs.
- Repeat with error bars; downgrade the two marginal wins to ties if they do
  not survive.
- Add end-to-end numbers *including* selection cost, not just per-cell.
- `UShER SARS-CoV-2` (8.45M genomes, open, 678 MB variant-major VCF) is the one
  real *genomic* dataset that reaches the target regime; density unmeasured.
- msprime/stdpopsim for a principled scale sweep, replacing the hand-rolled
  1/i draw in `storm_gen.cpp`.
