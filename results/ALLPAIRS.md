# All-pairs intersection: the tile pipeline

Consolidated record of the work in `RESEARCH_PLAN.md` §15.9–15.20 (claims
C13–C21). Harness: `bench/bench_tile.cpp`, `bench/bench_bloom.cpp`.
Corpora: [`data/README.md`](../data/README.md). Pair-level results:
[`CORPORA.md`](CORPORA.md), filter ablation: [`filter/`](filter/).

---

## 1. The finding that reframed the problem

Measured on the identical pair sample the timings use, 20 000 stride-sampled
pairs per corpus:

| corpus | pairs with \|A∩B\| = 0 | mean \|A∩B\| |
|---|---:|---:|
| dimension_003, uscensus2000 | **100.0%** | 0.00 |
| com-LiveJournal, soc-Pokec | **99.9%** | 0.00 |
| com-Orkut | 99.5% | 0.01 |
| as-skitter | 99.4% | 0.02 |
| census1881 | 96.9% | 0.76 |
| wiki-Talk | 95.1% | 0.11 |
| weather_sept_85 | 34.7% | 4025.78 |
| census-income | 24.8% | 5927.27 |

**In the target regime the answer is zero 95–100% of the time.** The operation
being optimised is therefore *disjointness proof*, not intersection — and every
kernel in the pairing matrix was built for the latter.

This is not a sampling artifact. It is what all-pairs means on a large sparse
universe, where E[|A∩B|] ≈ |A||B|/m is 1.3e-4 for as-skitter.

**Caveat:** this is *uniform* all-pairs. A windowed workload — LD within a
genomic window, neighbours-of-neighbours — deliberately selects correlated pairs
and would have a far lower disjoint rate.

---

## 2. The pipeline

```
per corpus, once:
  gate            measure survival + touch on a strided, probe-sized sample
                  survival < 0.15 AND touch < 0.25  ->  filter path
                  otherwise                          ->  bypass path

filter path, per tile of 64 rows (2016 pairs):
  1. sort rows within the tile by minimum element     (makes range monotone)
  2. range      reject pairs whose [min,max] cannot meet; break at first
                non-overlapping j
  3. matrix     if >25% survived, transpose the tile's zone maps and resolve
                all pairs' bucket overlap in one sparse pass
                - built once per tile, amortised across every block it joins
                - visit only multi-occupancy buckets (0.08-5.6% of buckets)
                - derive the touched-row mask; clear and scan only those rows
  4. probe      exact gated B x S on surviving pairs only

bypass path:
  route to the cell the pairing matrix selects for this density
  (B x B zonemap above ~1e-2 density) -- NOT the default B x S
```

Every stage is exact: it only ever skips work provably empty, and every variant
is checked against an oracle over the identical pair set before timing.

---

## 3. Results

### Filter path — sparse corpora

| corpus | unfiltered B×S | final | speedup |
|---|---:|---:|---:|
| dimension_003 | 56.68 ns | **0.010 ns** | ~5 700× |
| soc-Pokec | 55.80 ns | **0.098 ns** | ~570× |
| com-LiveJournal | 42.60 ns | **0.214 ns** | ~200× |
| as-skitter | 42.82 ns | **0.408 ns** | ~105× |
| com-Orkut | 149.13 ns | **2.571 ns** | ~58× |
| wiki-Talk | 69.18 ns | **9.762 ns** | ~7× |

### Bypass path — dense corpora, after cell routing (C21b)

| corpus | B×S (previous fallback) | B×B zonemap | gain |
|---|---:|---:|---:|
| dimension_033 | 2522.1 ns | **47.8 ns** | **52.8×** |
| census-income | 2382.8 ns | **323.9 ns** | **7.4×** |
| weather_sept_85 | 4613.7 ns | **1115.0 ns** | **4.1×** |

S×S and R×R are 6–25× *worse* than B×S on these corpora, confirming the density
map: above ~1e-2 density the dense cells win and every sparse representation
loses.

---

## 4. How each component was found

A width pass over five angles, then depth on those with alpha. Nineteen
iterations; the loop terminated on five consecutive non-improvements.

**Width pass** (speedup over unfiltered B×S, per-tile build charged):

| corpus | incumbent | range | hoist | matrix | group |
|---|---:|---:|---:|---:|---:|
| com-LiveJournal | 5.89 | 5.92 | 6.19 | 7.61 | **7.78** |
| uscensus2000 | 6.73 | 7.02 | 6.60 | 12.53 | **13.45** |
| dimension_003 | 1.22 | **48.44** | 1.24 | 13.62 | 2.40 |
| soc-Pokec | 6.18 | 6.23 | 6.16 | **7.38** | 5.74 |
| as-skitter | **3.96** | 3.52 | 3.40 | 3.79 | 3.58 |
| wiki-Talk | 2.25 | 2.17 | **2.50** | 2.17 | 1.89 |
| com-Orkut | 4.10 | 4.44 | 4.23 | 4.11 | **4.66** |

`range` — two integer comparisons on stored [min,max] — is the largest single
number in the project. `hoist` was marginal and dropped.

**Depth, improvements:** cascade ordering (§3), adaptive stage selection,
within-tile sorting, the gate on the tile pipeline, multi-occupancy resolve,
bucket width, direct pair emission.

---

## 5. What failed, and why it matters

Ten of nineteen iterations produced no improvement. Recorded because the pattern
is the result:

| attempt | outcome | why |
|---|---|---|
| Hashed Bloom filter | lost 9/10 to a size-matched positional map | hashing destroys the positional clustering bucketing exploits; cheaper probe beats better selectivity |
| Blocked Bloom | lost 10/10 to plain | at 2 kB the filter spans ~32 lines; blocking saves little and the extra hash costs every probe |
| Adaptive filter width ∝ \|A\| | lost 7/9 | survival, not footprint, dominates while both fit cache |
| Group-collapsing probes | 0.13× vs 0.41× | serial dependent loop with a per-element branch loses to branchless probing doing more lookups |
| Probe-and-commit for the gate | worse than static proxies | sample-timing overstates wide filters; the filter set stays resident over 1 000 sample pairs but is evicted over 20 000 |
| Naive composition (range × matrix) | 13.8× vs 38.5× | the transpose was built after range had already emptied the tile; prunes cost and must be ordered |
| Global row sorting | 0.89×, dimension_003 to 0.47× | sorting by `lo` clusters *similar* extents into a tile, maximising overlap — backwards from the prediction |
| Wide tiles (T=256) | 1.20× but loses on 2/5 | transposed tile 4× larger, candidate rows 4 words; working set offsets the arithmetic saving |
| Prefetch surviving pairs | 1.5–1.8× **slower** | prefetching in front of a filter fetches exactly the work the filter exists to avoid |
| Dual-tree over rows | prunes 0.0% at 256 rows/node | union filters saturate quadratically — see §6 |
| Hierarchical zone map (position axis) | 0.01–0.41×, i.e. 2.4–100× slower | the compact multi-occupancy list has already removed every empty and singleton bucket; a coarse level has nothing to prune and adds a dense scan |
| Dedup / unroll / popcount-sort / pc2 | 1.01–1.03× | inside the run-to-run noise band |

**The pattern: every improvement came from removing work; none came from
executing it faster.** Winners were the range predicate, transposing the tile,
visiting only multi-occupancy buckets, and scaling per-tile cost to occupied
rows. Every attempt at a faster *mechanism* failed. That reproduces at tile level
what C1 found at pair level — a SIMD-throughput kernel wins 0 of 25 asymmetric
measurement points.

---

## 6. Two structural results

### Bucket width tunes in opposite directions in the two regimes (C18)

Per-pair (C15), a fixed 2 kB filter beat wider ones: the probe must hold the
filter in cache on *every* pair. In the tile regime, 1M bits (128 kB/row) beats
2 kB by 2.2–17.7×. Neither pressure survives: the transpose is built once and
amortised, and the resolve touches only multi-occupancy buckets — of which a
wider filter produces *fewer*, since collisions are what create them. Widening
cuts the resolve and the false-candidate rate together.

Same structure, opposite tuning. That is a result about the access pattern, not
about the data.

### Row hierarchies saturate quadratically (C21a)

Dual-tree traversal over rows — prune |A|×|B| pairs when two nodes' union
filters are disjoint — measured at 1M buckets:

| node size | fill | node-pairs prunable |
|---:|---:|---:|
| 16 rows | 0.02–0.04% | **75–89%** |
| 64 rows | 0.08–0.14% | 14–41% |
| 256 rows | 0.3–0.6% | **0.0%** |
| 1024 rows | 1.2–2.2% | **0.0%** |

Union size grows linearly in node size *k*; P(disjoint) ≈ exp(−n²/NB) with
n ≈ 15k. At k=256 that is exp(−14.7) ≈ 4e-7, matching the measured zero. **A
dual-tree has at most two useful levels, and the 64-row tile already sits at the
saturation limit — the tile structure *is* the hierarchy.** Widening pushes
saturation out only as √NB: 4× deeper nodes for 16× the memory.

Skip pointers over *elements* (Moffat & Zobel 1996) are separately subsumed —
the zone map skips at O(1) per element, which O(log n) cannot beat.

The output-sensitivity a hierarchy would provide is already present: the
transpose iterates *buckets* and emits the pairs sharing one, at cost
Σ popcount²(w) rather than N².

---

## 7. Limitations

These bound every number above and are not minor.

1. **The transpose build is excluded**, justified by amortisation across the N/T
   blocks a tile joins (15 625 at a million rows). Valid for genuine all-pairs;
   for a single-block workload the build would dominate outright.
2. **Only within-tile pairs are measured** — the block diagonal, not the full
   all-pairs computation. Cross-tile blocks are untested.
3. **Bucket width is still a tuned parameter.** A universe-derived rule
   recovered only 42–102% of the tuned gain, so cardinality and clustering must
   enter it. Until that is solved the tile pipeline is **not deployable on
   unknown data** in the sense the pair-level gate achieves.
4. **Memory**: 128 kB/row of per-row zone maps at 1M buckets. The 8 MB per-tile
   transpose was previously listed here — that was peak *build* memory reported
   as resident. It exists only to construct the compact multi-occupancy list
   (0.02–0.82 kB/tile) and can be freed, a 7,000–400,000× reduction at zero
   runtime cost (C22b).
5. **One microarchitecture** (Apple M4). Zone-map behaviour is known to differ on
   x86 — growth is ARM-only and flat on Sapphire Rapids.
6. **T is hardwired to 64** by the 64-bit candidate words.

---

## 8. Consequence for the paper

The tile work and the pairing matrix are not two contributions but one: they
compose, and **the composition point is the selector**. C21b is the evidence —
the bypass path was defaulting to B×S while the pairing matrix already held the
right answer for those corpora, and consulting it was worth 4.1–52.8×.

The gate therefore needs two outputs, not one: *whether* to filter, and *which
cell* to run. Stated that way, the density map is not a static result but the
lookup table the runtime consults.
