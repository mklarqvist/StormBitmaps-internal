[![Github Releases](https://img.shields.io/github/release/mklarqvist/StormBitmaps.svg)](https://github.com/mklarqvist/StormBitmaps/releases)
[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)

<p align="center"><img src="storm-logo.jpg" alt="Storm" width="420"></p>

# Storm bitmaps

These algorithms and bitmaps are used to compute XX<sup>T</sup> for a _binary_ input matrix with dimensions (N,M) using specialized CPU instructions i.e.
[POPCNT](https://en.wikipedia.org/wiki/SSE4#POPCNT_and_LZCNT),
[SSE4.2](https://en.wikipedia.org/wiki/SSE4#SSE4.2),
[AVX2](https://en.wikipedia.org/wiki/Advanced_Vector_Extensions),
[AVX512BW](https://en.wikipedia.org/wiki/Advanced_Vector_Extensions). This is equivalent to computing the all-vs-all set intersection cardinality (|X<sub>i</sub> ∩ X<sup>T</sup><sub>j</sub>|) for pairs of _symmetric_ integer sets. These algorithms are fast in the worst case and _extremely_ fast when the input matrix is sparse.

> **Status (2026-08):** actively developed. The results above supersede the
> 2019 performance figures, which were removed: they were produced with a
> a constant-rate reference counter rather than a cycle counter, on
> hardware no longer available, and described a NEON path the pinned
> `libalgebra` does not contain. See [`PROBLEM_STATEMENT.md`](PROBLEM_STATEMENT.md)
> for the objective, [`RESEARCH_PLAN.md`](RESEARCH_PLAN.md) §16 for the current
> direction, and [`LANDSCAPE.md`](LANDSCAPE.md) §8 for the Phase 0 defects.

![screenshot](binary_matrix_multiplication.jpg)

Using large registers (AVX-512BW), contiguous and aligned memory, and
cache-aware blocking, we can achieve ~114 GB/s (~0.2 CPU cycles / 64-bit word)
of sustained throughput (~14 billion 64-bit bitmaps / second or up to ~912
billion implicit integers / second) using `STORM_contiguous_t` when the input
data is small (N < 256,000). When input data is large, we can achieve around
0.4-0.6 CPU cycles / 64-bit word using `STORM_t` while using considerably less
memory. Both of these models make use of scalar-bitmap or scalar-scalar
comparisons when the data density is small. Storm selects the optimal memory
alignment and subroutines given the available SIMD instruction at run-time by
using [libalgebra](https://github.com/mklarqvist/libalgebra).

The core algorithms are described in the papers:

* [Faster Population Counts using AVX2 Instructions](https://arxiv.org/abs/1611.07612) by Daniel Lemire, Nathan Kurz
  and Wojciech Muła (23 Nov 2016).
* Efficient Computation of Positional Population Counts Using SIMD Instructions,
  by Marcus D. R. Klarqvist, Wojciech Muła, and Daniel Lemire (upcoming)
* [Consistently faster and smaller compressed bitmaps with Roaring](https://arxiv.org/abs/1603.06549) by D. Lemire, G. Ssi-Yan-Kai,
  and O. Kaser (21 Mar 2016).

## Results

All-pairs set-intersection cardinality over 23 real corpora spanning IR posting
lists, web and social graphs, census and attribute data, sensor logs, and human
and viral genomics — universes from 2.0e5 to 1.3e8, densities from 7.9e-07 to
1.7e-01.

Every configuration is reported. A single number for a single configuration is a
result about the corpora chosen, not about the method.

### 1. Exact cardinality, selection + zone maps (the default)

Per-tile / probe-and-commit selection against a genuine unfiltered all-bitmap
baseline. Exact `|A n B|` for every pair.

| corpus | domain | universe | all-bitmap ns/pair | selected ns/pair | **speedup** |
|---|---|---:|---:|---:|---:|
| `enwiki-categorylinks` | IR | 129,698,523 | 642,867 | 593.6 | **1,083x** |
| `uscensus2000` | census | 36,974,578 | 123,380 | 211.1 | **584x** |
| `livejournal-groupmemberships` | graph | 7,489,074 | 23,533 | 45.0 | **523x** |
| `dimension_003` | druid | 3,866,847 | 10,148 | 24.7 | **410x** |
| `wikipedia_link_en` | graph | 11,206,012 | 38,892 | 96.3 | **404x** |
| `com-LiveJournal` | graph | 4,036,538 | 10,987 | 27.4 | **401x** |
| `census1881_srt` | census | 4,277,735 | 11,023 | 32.1 | **343x** |
| `dbpedia-link` | graph | 18,268,993 | 52,420 | 168.3 | **311x** |
| `as-skitter` | graph | 1,696,415 | 4,240 | 16.0 | **266x** |
| `gnomad_chr21_exomes_af1e-3` | genomics | 1,461,894 | 5,485 | 24.7 | **222x** |
| `soc-Pokec` | graph | 1,632,804 | 4,002 | 19.8 | **202x** |
| `dimension_008` | druid | 3,866,845 | 9,699 | 48.5 | **200x** |
| `wiki-Talk` | graph | 2,394,385 | 6,218 | 32.2 | **193x** |
| `com-Orkut` | graph | 3,072,627 | 8,381 | 48.6 | **172x** |
| `dimension_033` | druid | 3,866,847 | 10,028 | 60.2 | **167x** |
| `census1881` | census | 4,277,806 | 10,781 | 69.2 | **156x** |
| `usher_sarscov2` | genomics | 8,451,771 | 25,789 | 212.5 | **121x** |
| `wikileaks-noquotes` | text | 1,353,179 | 3,338 | 64.2 | **52x** |
| `msprime_1M` | genomics-sim | 2,000,000 | 10,498 | 2,394.4 | **4x** |
| `msprime_100k` | genomics-sim | 200,000 | 557 | 198.2 | **3x** |
| `weather_sept_85` | sensor | 1,015,367 | 2,984 | 1,368.0 | **2x** |
| `msprime_10k` | genomics-sim | 20,000 | 51 | 25.7 | **2x** |
| `census-income` | census | 199,523 | 386 | 257.9 | **1x** |

### 2. Exact cardinality, selection only — zone maps ablated

The same selector with the overlap filter removed.

| corpus | domain | universe | speedup (selection only) |
|---|---|---:|---:|
| `enwiki-categorylinks` | IR | 129,698,523 | 1.29x |
| `uscensus2000` | census | 36,974,578 | 1.07x |
| `livejournal-groupmemberships` | graph | 7,489,074 | 1.41x |
| `dimension_003` | druid | 3,866,847 | 1.01x |
| `wikipedia_link_en` | graph | 11,206,012 | 1.06x |
| `com-LiveJournal` | graph | 4,036,538 | 1.27x |
| `census1881_srt` | census | 4,277,735 | 1.37x |
| `dbpedia-link` | graph | 18,268,993 | 1.02x |
| `as-skitter` | graph | 1,696,415 | 0.97x |
| `gnomad_chr21_exomes_af1e-3` | genomics | 1,461,894 | 1.33x |
| `soc-Pokec` | graph | 1,632,804 | 0.97x |
| `dimension_008` | druid | 3,866,845 | 1.14x |
| `wiki-Talk` | graph | 2,394,385 | 2.17x |
| `com-Orkut` | graph | 3,072,627 | 1.02x |
| `dimension_033` | druid | 3,866,847 | 1.26x |
| `census1881` | census | 4,277,806 | 1.33x |
| `usher_sarscov2` | genomics | 8,451,771 | 1.96x |
| `wikileaks-noquotes` | text | 1,353,179 | 2.04x |
| `msprime_1M` | genomics-sim | 2,000,000 | 2.60x |
| `msprime_100k` | genomics-sim | 200,000 | 2.58x |
| `weather_sept_85` | sensor | 1,015,367 | 1.43x |
| `msprime_10k` | genomics-sim | 20,000 | 2.60x |
| `census-income` | census | 199,523 | 1.56x |

**Read this table with care.** It is *not* "selection is worthless without the
filter". `CostModel` is calibrated assuming B x B is zone-mapped, so removing the
kernel leaves the selector making choices that are wrong for the kernel it is
now running. Kernel and model are not separable without recalibration, and this
column measures the pair, not the filter. It is included because the filter is
optional and every configuration should be visible — but the honest reading is
that the zone map and the cost model are coupled, and that coupling is itself a
limitation.

### 3. Thresholded mode (opt-in) — only pairs with Jaccard >= t

Exact cardinalities, restricted to pairs above a caller-supplied similarity
cutoff, via prefix filtering (Bayardo et al. WWW 2007). **Speedup is against the
exact scan, not against all-bitmap**, so it composes with table 1 rather than
replacing it.

| corpus | domain | pairs | t=0.001 | t=0.01 | t=0.1 |
|---|---|---:|---:|---:|---:|
| `com-LiveJournal` | graph | 73,536 | **168.0x** | **319.1x** | **317.0x** |
| `soc-Pokec` | graph | 73,536 | **169.8x** | **133.1x** | **184.4x** |
| `dbpedia-link` | graph | 73,536 | **31.9x** | **29.2x** | **163.9x** |
| `com-Orkut` | graph | 73,536 | **49.2x** | **60.0x** | **133.9x** |
| `gnomad_chr21_exomes_af1e-3` | genomics | 73,536 | **68.5x** | **101.4x** | **127.0x** |
| `as-skitter` | graph | 73,536 | **119.0x** | **104.6x** | **126.9x** |
| `wikipedia_link_en` | graph | 73,536 | **11.9x** | **16.4x** | **120.1x** |
| `livejournal-groupmemberships` | graph | 73,536 | **26.5x** | **37.9x** | **42.6x** |
| `dimension_003` | druid | 73,536 | **31.9x** | **35.0x** | **29.0x** |
| `wiki-Talk` | graph | 73,536 | **10.5x** | **11.3x** | **32.8x** |
| `uscensus2000` | census | 19,900 | **25.9x** | **17.6x** | **32.3x** |
| `wikileaks-noquotes` | text | 19,900 | **4.7x** | **3.5x** | **10.2x** |
| `usher_sarscov2` | genomics | 73,536 | 1.00x | 1.00x | **3.0x** |
| `enwiki-categorylinks` | IR | 2,701 | 0.99x | **2.2x** | 0.78x |
| `dimension_033` | druid | 14,878 | **1.9x** | **2.1x** | **2.1x** |
| `census1881_srt` | census | 19,900 | **1.2x** | **1.9x** | **1.9x** |
| `census1881` | census | 19,900 | 1.04x | 0.95x | **1.3x** |
| `dimension_008` | druid | 73,536 | 1.00x | 1.00x | 1.00x |
| `weather_sept_85` | sensor | 19,900 | 1.00x | 1.00x | 1.00x |
| `census-income` | census | 19,900 | 1.00x | 1.00x | 1.00x |
| `msprime_1M` | genomics-sim | 73,536 | 1.00x | 1.00x | 1.00x |
| `msprime_100k` | genomics-sim | 73,536 | 1.00x | 1.00x | 1.00x |
| `msprime_10k` | genomics-sim | 73,536 | 1.00x | 1.00x | 1.00x |

Gated: where prefix filtering would lose, the gate falls back to the exact scan
and the entry reads 1.00x. `msprime_*`, `weather_sept_85`, `census-income` and
`dimension_008` bypass at every threshold — at densities of 6-17% almost every
pair clears any cutoff, so there is nothing to skip.

**`enwiki-categorylinks` is understated here.** The memory budget allows only 74
rows at universe 1.3e8, giving 2,701 pairs against an index build of 19,537
operations — 7.2 ops per pair. The index build is O(N) while pairs are O(N^2), so
on the real 2.17M-row corpus it is negligible; at 74 rows it dominates. The same
applies in weaker form to every corpus above ~1e7 universe.

### 4. Storage cost — bits per value, data and metadata separated

Reported the way the Roaring papers do so corpora of different cardinality are
comparable. **Metadata is listed separately and deliberately**: the zone map and
rank index are not part of any representation, they are what the selector needs
in order to choose one, and folding them into the data column would hide the
cost of the mechanism that is new here.

| corpus | universe | bitmap | array | runs | EWAH | **best** |
|---|---:|---:|---:|---:|---:|---:|
| `census-income` | 199,523 | 5.77 | 32.00 | 20.73 | 3.86 | **3.08** |
| `msprime_100k` | 200,000 | 12.57 | 32.00 | 31.88 | 5.29 | **4.44** |
| `weather_sept_85` | 1,015,367 | 15.78 | 32.00 | 30.06 | 7.87 | **6.77** |
| `usher_sarscov2` | 8,451,771 | 1,080.60 | 32.00 | 2.04 | 4.06 | **2.02** |
| `wikileaks-noquotes` | 1,353,179 | 982.89 | 32.00 | 11.36 | 19.46 | **10.64** |
| `census1881` | 4,277,806 | 852.27 | 32.00 | 58.86 | 43.79 | **29.92** |
| `as-skitter` | 1,696,415 | 1.26e5 | 32.00 | 47.13 | 77.72 | **29.26** |
| `wikipedia_link_en` | 11,206,012 | 2.68e5 | 32.00 | 46.21 | 72.90 | **28.27** |
| `dbpedia-link` | 18,268,993 | 6.38e5 | 32.00 | 56.38 | 101.63 | **31.84** |
| `uscensus2000` | 36,974,578 | 1.24e6 | 32.00 | 57.78 | 91.89 | **31.98** |
| `enwiki-categorylinks` | 129,698,523 | 3.33e6 | 32.00 | 62.72 | 113.23 | **31.85** |

On dense corpora the numbers are Roaring-comparable (3–7 bits/value). On sparse
ones the dense bitmap costs 10^3–10^6 bits per value, which is the storage
statement of the same fact the timing tables make: **a bitmap over a large sparse
universe is not a compression scheme, it is a liability.** `usher_sarscov2` is
the standout at 2.02 bits/value — its carriers cluster into long runs.

#### Selector metadata is universe-proportional, and that is a defect

| corpus | universe | meta B/row | data B/row | ratio |
|---|---:|---:|---:|---:|
| `census-income` | 199,523 | 6,335 | 13,344 | 0.5x |
| `weather_sept_85` | 1,015,367 | 32,031 | 54,497 | 0.6x |
| `census1881` | 4,277,806 | 134,784 | 18,774 | 7.2x |
| `as-skitter` | 1,696,415 | 53,479 | 49 | **1,083x** |
| `dbpedia-link` | 18,268,993 | 575,415 | 113 | **5,051x** |
| `enwiki-categorylinks` | 129,698,523 | 4,084,799 | 155 | **26,312x** |

`build_row()` constructs the rank index and zone map unconditionally, and both
scale with **universe**, not cardinality. On `enwiki-categorylinks` that is 4 MB
of selector metadata attached to 155 bytes of data. The complement
representation already carries the right guard — it is built only when
`2n > universe` — and rank and occupancy have none. **This is a known defect, not
a design choice**, and it is why bitmap-bearing measurements are restricted to
tiny row counts on the large-universe corpora. Fixing it is a prerequisite for
the C ABI (`RESEARCH_PLAN.md` §16.3).

### What the tables show

- **Density drives everything.** Below ~1e-5 the speedup is 200-1000x; above
  ~1e-2 it collapses to 1.5-4x. That gradient *is* the contribution.
- **No single representation wins.** On `msprime_1M`, B x S is 843x faster than
  B x B on pairs where `min(|A|,|B|) <= 10` and 18x *slower* where it exceeds
  1e5. Choosing per pair is the whole design.
- **All-bitmap is infeasible, not merely slow, at scale.** At universe 1.3e8 a
  row is 15.8 MB, so a bitmap AND moves 32 MB per pair — 643 microseconds to
  compute an answer that is zero 100% of the time.
- **Selection costs 0.02-0.4% of runtime** (Gate 1 budget: 2%). Per-pair
  selection fails that budget; per-tile and probe-and-commit pass.

## All-pairs: the tile pipeline

Full record: [`results/ALLPAIRS.md`](results/ALLPAIRS.md) · harness:
`bench/bench_tile.cpp`

**In the target regime 95–100% of pairs have an empty intersection**, so the
operation being optimised is disjointness *proof*, not intersection. Exploiting
the all-pairs structure — rather than the per-pair constant — is worth one to
three orders of magnitude:

| corpus | unfiltered B×S | tile pipeline | speedup |
|---|---:|---:|---:|
| dimension_003 | 56.68 ns | **0.010 ns** | ~5,700× |
| soc-Pokec | 55.80 ns | **0.098 ns** | ~570× |
| com-LiveJournal | 42.60 ns | **0.214 ns** | ~200× |
| as-skitter | 42.82 ns | **0.408 ns** | ~105× |
| com-Orkut | 149.13 ns | **2.571 ns** | ~58× |
| dimension_033 | 2522.1 ns | **47.8 ns** | ~53× |
| wiki-Talk | 69.18 ns | **9.762 ns** | ~7× |
| census-income | 2382.8 ns | **323.9 ns** | ~7× |
| weather_sept_85 | 4613.7 ns | **1115.0 ns** | ~4× |

The pipeline gates on measured selectivity, then for tiles it accepts: sorts
rows by minimum, rejects pairs whose extents cannot meet, transposes the tile's
zone maps to resolve every pair's bucket overlap in one sparse pass, and probes
only survivors. Tiles it rejects are routed to the cell the pairing matrix
selects for that density.

**Every improvement across 19 iterations came from removing work; none came from
executing it faster.** Ten iterations produced no improvement and are recorded
with the reasons — including a dual-tree over rows, which prunes 89% of node
pairs at 16 rows/node and **0.0% at 256**, because union filters saturate
quadratically. Read [`results/ALLPAIRS.md`](results/ALLPAIRS.md) §5 and §7 before
quoting any figure above: the transpose build is excluded by an amortisation
argument, only within-tile pairs are measured, and bucket width is still tuned.

## Real-corpus benchmark vs CRoaring

Full record: [`results/CORPORA.md`](results/CORPORA.md) · raw runs:
[`results/corpora/`](results/corpora/) · reproduce: `bench/run_corpora.sh`

Seventeen real corpora. Twelve are the
[`real-roaring-datasets`](https://github.com/RoaringBitmap/real-roaring-datasets)
files **CRoaring's own benchmark harness runs by default** — so this is the
incumbent's chosen data, not ours — plus five larger modern graphs. CRoaring is
given `roaring_bitmap_run_optimize()` so it gets its run containers, and all
ratios are quoted against that tuned variant. Every kernel is checked against
`roaring_bitmap_and_cardinality` on every pair before anything is timed.

Storm's best cell beats tuned CRoaring on **17 of 17**. Ordered by density,
which is the independent variable — the deliverable is a map of which
representation pairing wins where, not one headline number.

| corpus | universe m | sets | mean \|Xi\| | density | best cell | ns/pair | vs CRoaring_ro | vs all-bitmap |
|---|---:|---:|---:|---:|---|---:|---:|---:|
| uscensus2000 | 36,974,578 | 200 | 30 | 8.09e-07 | B×S | 7.5 | **2.29x** | 24,919× |
| com-LiveJournal | 4,036,538 | 256 | 21 | 5.14e-06 | B×B | 28.8 | **2.04x** | 544× |
| as-skitter | 1,696,415 | 256 | 15 | 9.08e-06 | B×S | 7.2 | **4.02x** | 756× |
| soc-Pokec | 1,632,804 | 256 | 25 | 1.55e-05 | B×B | 27.1 | **4.36x** | 175× |
| wiki-Talk | 2,394,385 | 256 | 52 | 2.17e-05 | B×S | 9.2 | **8.80x** | 910× |
| dimension_003 | 3,866,847 | 256 | 91 | 2.36e-05 | S×S | 2.8 | **1.12x** | 4,042× |
| com-Orkut | 3,072,627 | 256 | 82 | 2.66e-05 | B×B | 39.7 | **6.07x** | 232× |
| dimension_008 | 3,866,845 | 256 | 347 | 8.98e-05 | R×R | 3.9 | **1.73x** | 2,846× |
| census1881_srt | 4,277,735 | 200 | 3,404 | 7.96e-04 | B×R | 30.4 | **2.33x** | 387× |
| wikileaks-noquotes | 1,353,179 | 200 | 1,377 | 1.02e-03 | B×B | 60.8 | **6.79x** | 67× |
| wikileaks-noquotes_srt | 1,353,133 | 200 | 1,440 | 1.06e-03 | B×B | 19.8 | **6.06x** | 223× |
| census1881 | 4,277,806 | 200 | 5,019 | 1.17e-03 | B×B | 79.5 | **16.06x** | 151× |
| dimension_033 | 3,866,847 | 173 | 22,352 | 5.78e-03 | B×R | 36.7 | **2.82x** | 290× |
| weather_sept_85 | 1,015,367 | 200 | 64,353 | 6.34e-02 | B×B | 1178.8 | **15.02x** | 2× |
| weather_sept_85_srt | 1,015,367 | 200 | 80,540 | 7.93e-02 | B×B | 322.7 | **5.44x** | 8× |
| census-income_srt | 199,523 | 200 | 30,464 | 1.53e-01 | B×B | 136.0 | **9.78x** | 3× |
| census-income | 199,523 | 200 | 34,610 | 1.73e-01 | B×B | 301.9 | **13.11x** | 1× |

**The winning cell distribution is the actual result**: B×B ×10, B×S ×3,
B×R ×2, S×S ×1, R×R ×1. No representation pairing dominates. Had one done so,
the pairing matrix would be unnecessary. The per-cell matrix in
[`results/corpora/SUMMARY.md`](results/corpora/SUMMARY.md) shows the cost of
choosing wrong: on `dimension_033`, B×B gives 1.9× and S×S gives 0.02× — a 95×
penalty on identical data. That gap is what a selector has to earn back.

W×W (EWAH) is below 1.0× on 16 of 17 and is kept as a **labelled loser**, not a
contender.

### Why Storm also wins on the *dense* corpora

At 6–17% density both sides should be doing near-identical popcount work. They
are not. A container census via `roaring_bitmap_statistics()` after
`run_optimize()`:

| corpus | array | bitset | run | global density |
|---|---:|---:|---:|---:|
| weather_sept_85 | **2,274** | 561 | 21 | 6.3e-02 |
| census-income | **553** | 180 | 35 | 1.7e-01 |
| census1881 | **1,332** | 0 | 132 | 1.2e-03 |
| uscensus2000 | **2,219** | 0 | 2 | 8.1e-07 |

Roaring picks its container by testing **per-2¹⁶-chunk cardinality against a
fixed threshold of 4096**. With a large universe the set mass spreads thinly
enough that chunks stay under that threshold even when *global* density is 6% or
17%, so CRoaring runs array merges where Storm popcounts. census1881 has **zero**
bitset containers at density 1.2e-3. This is a "do the right kind of work"
result, not a SIMD result.

### Ablation: does the zone map earn its keep?

The zone map (1 bit per 512-bit bin) is built unconditionally by `build_row()`,
so the table above cannot answer this — it only ever shows the planned kernel.
`bench_baseline` therefore also times each cell's best **index-free** variant
against its index-using one. Full table:
[`results/corpora/ABLATION.md`](results/corpora/ABLATION.md).

**Net: helps 12/17, neutral 5/17, hurts 0/17, at 0.195% of bitmap bytes.**

| | |
|---|---|
| net gain, best-cell level | 1.00×–3.80× |
| B×B in isolation | up to 745× — but mostly on corpora where B×B loses anyway |
| B×S in isolation | **below 1.0× on 10 of 17** — gating bins costs more than the probes it saves |
| largest gains | both ends of the density range; middle mostly neutral |
| `_srt` vs unsorted | sorted variants gain more (3.80 vs 2.49, 3.48 vs 2.21) — clustering is what empties bins |

Two things follow. The five neutral corpora are exactly those where a sparse
cell (B×S, S×S, R×R) wins and reads no side structure — the zone map is built,
unused, and paid for only in space. And it never loses at system level *because*
selection routes around it: per-cell it can lose badly (B×S with zone map on
census1881 is 0.25×). The zone map is not independently a good idea; it is a
good idea **given** a selector that can decline it.

### Measurement caveats — read before quoting any number above

1. **Ratios are reliable to about ±1 significant figure, no better.** The full
   sweep was run twice: both runs win 17/17, but ratios drift by a median of
   1.15× and up to 1.65×, and **the winning cell changes on 5 of 17**. Three of
   those five are harmless near-ties (census1881 run 1: B×B 15.81 vs B×S 15.83);
   `uscensus2000` and `dimension_033` are not, moving both winner and magnitude,
   and should not be quoted until the harness is fixed. Repeating single corpora
   as independent processes gives spreads of 16.1–18.2× (census1881) and
   **1.2–3.7× (dimension_003)** — the fastest corpora are the noisiest, since at
   2.9 ns/pair a 20,000-pair batch lasts only 58 µs.
2. **The table is systematically pessimistic.** Values were collected in one
   back-to-back batch of 17 corpora; standalone repeats land at or *above* the
   recorded figure in every case checked. Likely thermal accumulation and
   cross-corpus cache pollution.
3. **One microarchitecture** (Apple M4). Zone-map growth is known ARM-only and
   flat on Sapphire Rapids, so x86 numbers will differ and must be measured.
4. **Selection cost is excluded** — cells are timed directly. The near-free
   selection claim is measured separately.
5. **`vs all-bitmap` is a weak baseline** at large universe (104 MB for
   census1881 at 200 rows). The CRoaring column is the defensible one.

The fix for (1) and (2) is interleaved round-robin timing across corpora plus
median-and-IQR over N independent runs, which is what `bench_cells` already does
per-variant. Until that lands, treat the ordering as sound and the exact ratios
as provisional.

## API

The interface is found in the file `storm.h`.

```c
#include "storm.h"

int intcmp(const void* aa, const void* bb) {
    const uint32_t* a = aa, *b = bb;
    return (*a < *b) ? 0 : (*a > *b);
}

int main() {
    uint32_t n_vectors = 10000; // number of rows
    uint32_t n_width = 1000; // number of columns
    uint32_t n_bits_set = 128; // number of bits set per row

    STORM_t* storm = STORM_new(); // STORM model
    STORM_contiguous_t* storm_cont = STORM_contig_new(n_width); // STORM contiguous memory
    uint32_t* vals = malloc(n_bits_set*sizeof(uint32_t)); // array for random values

    // Foreach vector (row)
    for (int i = 0; i < n_vectors; ++i) {
        // Generate random bits for each row
        for (int j = 0; j < n_bits_set; ++j) {
            vals[j] = rand() % n_width; // draw random number
        }
        // Input data must be sorted.
        qsort(vals, n_bits_set, sizeof(uint32_t), intcmp);
        
        // Add data to either model.
        STORM_add(storm, &vals[0], n_bits_set);
        STORM_contig_add(storm_cont, &vals[0], n_bits_set);
    }

    uint64_t cstorm      = STORM_pairw_intersect_cardinality(storm);
    uint64_t cstorm_cont = STORM_contig_pairw_intersect_cardinality(storm_cont);

    printf("contig=%llu storm=%llu\n",cstorm_cont,cstorm);

    free(vals);
    STORM_free(storm);
    STORM_contig_free(storm_cont);
    return 1;
}
```

## Compilation

StormBitmaps can be compiled using the standard `cmake` workflow:
```bash
cmake .
make
```

As with all `cmake` projects, you can specify the compilers you wish to use by
adding (for example) `-DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++` to the
`cmake` command line. We tell the compiler to target the architecture of the
build machine by using the `-march=native` flag. This can be disabled by passing
the `-DSTORM_DISABLE_NATIVE=ON` argument to `cmake`.

On Linux and MacOSX, and when running with the native compilation flag, we do
not need to specify the target hardware instructions set. This is not the case
on Windows where we need to set these flags:

| `CMake` Flag               | Description                |
|--------------------------|----------------------------|
| `STORM_ENABLE_SIMD_AVX512` | Enable AVX512 instructions |
| `STORM_ENABLE_SIMD_AVX2` | Enable AVX256 instructions |
| `STORM_ENABLE_SIMD_SSE4_2` | Enable SSE4.2 instructions |

For example, we can run `cmake -DSTORM_ENABLE_SIMD_SSE4_2="ON" .` to enable SSE4.2 instructions.

and run `./benchmark`.
