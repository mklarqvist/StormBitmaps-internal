[![Github Releases](https://img.shields.io/github/release/mklarqvist/StormBitmaps.svg)](https://github.com/mklarqvist/StormBitmaps/releases)
[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)

# Storm bitmaps

These algorithms and bitmaps are used to compute XX<sup>T</sup> for a _binary_ input matrix with dimensions (N,M) using specialized CPU instructions i.e.
[POPCNT](https://en.wikipedia.org/wiki/SSE4#POPCNT_and_LZCNT),
[SSE4.2](https://en.wikipedia.org/wiki/SSE4#SSE4.2),
[AVX2](https://en.wikipedia.org/wiki/Advanced_Vector_Extensions),
[AVX512BW](https://en.wikipedia.org/wiki/Advanced_Vector_Extensions). This is equivalent to computing the all-vs-all set intersection cardinality (|X<sub>i</sub> ∩ X<sup>T</sup><sub>j</sub>|) for pairs of _symmetric_ integer sets. These algorithms are fast in the worst case and _extremely_ fast when the input matrix is sparse.

> **Status (2026-08):** this repository is being revived. See
> [`PROBLEM_STATEMENT.md`](PROBLEM_STATEMENT.md) for the current research objective and
> [`LANDSCAPE.md`](LANDSCAPE.md) §8 for the defects fixed in Phase 0.
>
> Two corrections to the claims below, both recorded in `LANDSCAPE.md`:
>
> * **NEON is not implemented.** The pinned `libalgebra` contains no NEON code path — the word
>   appears only in a comment. On AArch64 the library builds and is correct, but runs scalar
>   fallbacks. NEON/SVE2 support is planned, not present.
> * **The performance figures below are not cycles.** They were produced with a constant-rate
>   reference counter (x86 RDTSC), not a core cycle counter, and are unverified on current
>   hardware. Treat them as historical until re-measured with `perf`.

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

## Thresholded mode (opt-in)

Full record: [`results/THRESHOLD.md`](results/THRESHOLD.md) · figure:
`results/threshold/sweep.png` · sweep: `bench/sweep_threshold.sh`

**Exact all-pairs is the default.** Some consumers need every exact cardinality;
others supply a similarity cutoff — LD for plotting wants pairs above an r²
threshold. When a threshold *is* available it unlocks the all-pairs
similarity-join family (Bayardo et al. WWW 2007; Xiao et al. WWW 2008), whose
prefix filter is **candidate generation rather than pair filtering** — it never
enumerates the N(N−1)/2 pairs at all.

Gated speedup at t=0.001, the conservative end (speedup rises with t; at t=0.5
the winners reach 292–722×):

| corpus | speedup | gate | pairs above threshold |
|---|---:|---|---:|
| com-LiveJournal | **89.6×** | prefix | 0.037% |
| soc-Pokec | **75.5×** | prefix | 0.083% |
| com-Orkut | **44.5×** | prefix | 0.444% |
| as-skitter | **41.7×** | prefix | 0.401% |
| uscensus2000 | **29.3×** | prefix | 0.000% |
| dimension_003 | **15.7×** | prefix | 0.000% |
| wiki-Talk | **6.3×** | prefix | 2.757% |
| wikileaks-noquotes | **3.8×** | prefix | 3.075% |
| dimension_008, weather_sept_85, census-income | 1.00× | exact | 39–62% |
| census1881, census1881_srt | 0.97×, 0.91× | prefix | 0.3% |

Reported cardinalities stay exact — only the *set* of reported pairs is
restricted, and all 650 sweep points are verified against the exact scan's hit
set. The gains track how much of the output the caller discards: the corpora
that win are those where 0.00–0.44% of pairs clear the threshold, and the three
that fall back to the exact scan are those where 39–62% do.

Unlike the exact-mode gate, this one is **not never-worse** — worst case is
≈0.91×. See [`results/THRESHOLD.md`](results/THRESHOLD.md) §5–6 before quoting
any figure.

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

## Performance

All performance tests were run on a host machine with a 10 nm Cannon Lake Core
i3-8121U with gcc (GCC) 8.2.1 20180905 (Red Hat 8.2.1-3). Detailed benchmarking requires the
Linux `perf` subsystem. In all the examples below we do not output the result matrix but instead report its total sum. This was done to restrict our measurements to algorithmic performance while being unaffacted by disk I/O.

### Small input matrix

Sample performance metrics (practical upper limit) for **dense** matrices using a host machine with AVX512BW available. We
simulate many data arrays in aligned memory and compute the upper triangular or XX<sup>T</sup>
using the command `benchmark 65536 10000` 

| Set bits | CPU cycles / 64-bit word | MB/s      |
|----------|--------------------------|-----------|
| 32768    | 0.209                    | 109915 |
| 16384    | 0.21                     | 113591 |
| 6553     | 0.21                     | 114524 |
| 2621     | 0.21                     | 114256 |
| 1310     | 0.21                     | 114625 |
| 655      | 0.21                     | 114709 |
| 262      | 0.21                     | 114659 |
| 65       | 0.21                     | 114390 |
| 13       | 0.21                     | 114726 |
| 5        | 0.21                     | 114574 |
| 1        | 0.21                     | 114457 |

### Large input matrix

Next we simulated 10,000 vectors with [0,262144] random bits set for each to form a (10000,524288)-dimension matrix with different data densities. The following (misleading) figures
represent CPU cycles / 64-bit word equivalent if the analysis was run in uncompressed **bitmap space**. We compare our simple approach to [Roaring bitmaps](https://github.com/RoaringBitmap/CRoaring) demonstrating we can achieve performance parity on large bitmaps:

| Set bits | Storm-CPU-cycles | Roaring-CPU-cycles |
|----------|------------------|--------------------|
| 262144   | 0.483            | 0.567              |
| 131072   | 0.477            | 0.567              |
| 52428    | 0.474            | 0.567              |
| 20971    | 3.431            | 3.342              |
| 10485    | 1.765            | 1.756              |
| 5242     | 0.935            | 0.945              |
| 2097     | 0.43             | 0.451              |
| 524      | 0.19             | 0.196              |
| 104      | 0.117            | 0.133              |
| 5        | 0.017            | 0.029              |
| 1        | 0.003            | 0.004              |

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

### Note

This is a collaborative effort between Marcus D. R. Klarqvist
([@mklarqvist](https://github.com/mklarqvist/)) and Daniel Lemire
([@lemire](https://github.com/lemire/)).

### History

These functions were originally developed for
[Tomahawk](https://github.com/mklarqvist/Tomahawk) for computing genome-wide
linkage-disequilibrium but can be applied to any intersect-count problem.