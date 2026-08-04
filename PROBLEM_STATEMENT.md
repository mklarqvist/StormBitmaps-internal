# Problem Statement

**This is the primary document. Read it before `RESEARCH_PLAN.md` or any code.**

Scope: kernels and algorithms only. No application layer.

---

## 1. The objective, stated once

> Compute all-pairs set-intersection cardinality `|Xᵢ ∩ Xⱼ|` by selecting, **for every pair and
> every region within a pair, the best-matched pair of representations** — bitmap, sorted scalar
> array, RLE/run, WAH-style fill-compressed, or Roaring container — and then vectorizing each
> distinct representation-pairing as its own specialized kernel.

The object of study is the **pairing matrix**: the cross-product of representations, where each
cell is a distinct kernel with a distinct cost function, and the algorithmic problem is choosing
the right cell cheaply at N² scale.

This is not "make popcount faster." Popcount is solved (Muła/Kurz/Lemire 2018). This is:
**stop spending fixed cost on pairs that deserve near-zero cost.**

---

## 2. Why bitmap × bitmap is the wrong default

`popcount(A AND B)` over dense bitmaps costs **Θ(m)** where `m = ⌈U/64⌉` words — **regardless of
how many bits are actually set.** It is a fixed cost. It does not care that one side is empty.

> **Scope of that statement, corrected 2026-08-04.** It describes the *naive* dense kernel — which
> is what every existing implementation runs, and what the argument below is about. It is **no
> longer true of this project's own best B × B kernel.** Zone-map planning (§2.6) makes the dense
> cell visit only the 512-bit bins live on both sides, which is sub-linear in `m` and measures
> 24.9× on run-structured data. The fixed-cost premise motivates the project; it no longer
> describes its output.

That is fine when both sides are dense. It is catastrophic when either side is sparse, and in real
data one side is *usually* sparse.

### 2.1 The motivating case: large-scale phased haplotype data

Take 10 million phased haplotypes. Each variant is a row: one bit per haplotype.

```
U = 10,000,000 bits
m = 156,250 × 64-bit words = 1.25 MB per row
```

Now consider a **singleton** — a variant carried by exactly one haplotype (allele count 1).

| Approach | Work per pair | Memory touched |
|---|---|---|
| bitmap × bitmap | 19,531 AVX-512 AND+POPCNT ops (~6.5 µs @ 3 GHz) | 2.5 MB |
| sorted array × sorted array | **1 comparison** (~1–5 cycles) | **8 bytes** |

Roughly **10⁴× in operations and 10⁵× in memory traffic** — to compute a result that is almost
always 0. Under bitmap × bitmap, essentially 100% of those cycles are spent ANDing zeros against
zeros.

### 2.2 This is the common case, not an edge case

The site frequency spectrum is dominated by rare variants. Under the standard neutral coalescent,
the expected number of variants at derived allele count `i` is proportional to **1/i**; in large
sequencing cohorts singletons are typically the single largest class, and rare variants
collectively dominate the catalogue.

So for a genome-wide all-pairs computation, **most rows are extremely sparse**, and therefore
**most pairs have at least one sparse side.**

### 2.3 What that does to total work

Suppose a fraction `p` of rows fall below the sparse/dense crossover. Then:

| Pair class | Share of pairs | Cost with adaptive pairing |
|---|---|---|
| sparse × sparse | `p²` | ≈ ε (proportional to cardinality) |
| **mixed (sparse × dense)** | `2p(1−p)` | **O(cardinality of the sparse side)** |
| dense × dense | `(1−p)²` | Θ(m) — unavoidable |

At `p = 0.9`: 81% of pairs collapse to near-nothing, 18% are mixed, 1% are genuinely dense.
Total work falls to roughly **1% of the all-bitmap cost — a ~100× overall reduction** — *provided
the mixed cell is handled well.*

**Correction (2026-08-04): this table is single-sided and the phenomenon is not.** A row that is
99.99% *ones* is exactly as compressible as one that is 0.01% ones — its complement is one run —
and the density sweep measures R × R winning **52.7×** at density 1.0. A single fraction `p` of
"sparse" rows cannot express a phenomenon symmetric about density ½. The three-class model should
be read as a sparse tail, a genuinely-mixed middle band (measured: ~0.4% to ~99.6% density, where
bitmap × bitmap is correctly the winner), and a dense tail that is the sparse tail of the
complement. See §2.5.

**The mixed cell is where the whole result lives.** It is 18% of pairs at `p = 0.9`, and it is
also the cell that is easiest to get wrong: the naive move is to inflate the sparse side into a
bitmap and fall back to bitmap × bitmap, which recovers **zero** of the saving on nearly a fifth
of all pairs. Asymmetric kernels are therefore the highest-value engineering in this project.

---

### 2.5 Measured (2026-08-04, Apple M4) — the claim of §2, tested

`results/density.{png,svg}`, regenerated from `results/density.jsonl` by
`bench/plot_density.py`. Twenty density points, one bit set through every bit
set, both sides at the same density, universe pinned so cache residency is
constant along the axis.

- **Bitmap × bitmap is flat at 115–150 ns/pair across five orders of magnitude
  of density.** The fixed-cost premise of §2 is now a measurement, not an
  argument.
- **At one bit set in 65,536, the best pairing costs 1.6 ns against that
  kernel's 114.9 — 70×**, and the ratio grows with the universe.
- **The crossover is at ~0.4% density.** Above it nothing beats bitmap × bitmap;
  below it the advantage compounds.
- **The curve is U-shaped.** Near density 1 the complement is sparse, the data
  is one long run, and R × R wins 52.7×. Compressed pairing wins at *both*
  extremes.

The last point corrects this document. §2 is written as though sparsity is the
operative variable; it is not. **Distance from density ½ is.** A row that is
99.99% ones is exactly as compressible as one that is 0.01% ones, and the
pairing matrix serves both. The selection model (M2) therefore needs a two-sided
test rather than a sparsity threshold, and the motivating haplotype case is one
tail of a symmetric phenomenon rather than the whole of it.

A second measured result, from the same campaign: across all five *asymmetric*
cells at all five corpus shapes — 25 measurement points — **a SIMD-throughput
kernel wins none of them.** The winners are scalar loops, rank lookups and
search-strategy selection. SIMD wins where the work is irreducible, which is
bitmap × bitmap and nowhere else. Faster popcount is a constant factor on the
one cell that cannot avoid the work; representation pairing is an asymptotic
factor on the other nine. Full record in `results/OPTLOG.md`.


### 2.6 Zone maps — deciding what work to do, instead of doing it faster

Added 2026-08-04, after measurement made it the strongest single mechanism in the project.

A **zone map** is one bit per 512-bit bin of a row, set when that bin contains anything. It costs
**0.195%** of the bitmap — against the rank index's 25% — and it is built in a single forward pass,
or stored alongside the data as columnar formats already store zone maps, in which case it is free
at query time.

For a pair, `occ_A AND occ_B` is a bitmap intersection over `m/512` bits. Its popcount is the
**exact** number of bins that need visiting — not an estimate — and zero *proves* the two rows
disjoint. Deciding what work to do therefore costs 1/512 of doing it.

Measured on B × B (`results/OPTLOG.md` F8, `results/CROSS_ISA.md`):

| corpus | best SIMD dense | zone-map planned |
|---|---:|---:|
| uniform density | 117.3 ns | 127.6 (0.92×) |
| 1/i spectrum | 118.7 | **48.5 (2.45×)** |
| clustered 1/i | 132.7 | **21.1 (6.3×)** |
| long runs | 455.4 | **18.3 (24.9×)** |

Three properties that matter:

1. **Applied unconditionally it is a 3× pessimization** on uniform-density data — every bin is
   occupied, the filter filters nothing, and the scan is pure overhead. A filter only pays when it
   filters.
2. **Consulted as a plan it is nearly free to be wrong**, because the selectivity scan is exact.
   That recovers the uniform case to 0.92× while keeping 25× at the sparse end.
3. **It does not generalize.** It transfers to B × W but *fails* on B × S (0.23–0.59×) and B × R,
   because those cells are already work-proportional to the sparse side — a summary can only
   remove probes, not scans. Summaries pay where a *dense* representation is being scanned.

**Prior art, stated plainly.** The mechanism is not new. BitFunnel (Goodwin et al., SIGIR 2017)
builds a hierarchical occupancy index and ANDs it top-down to discard blocks before touching the
dense bits beneath; WAH/EWAH's own uniform-word fills skip empty stretches by construction; Lucene's
`SparseFixedBitSet` keeps a bitmap of non-empty blocks. What is unclaimed is this parameterization
applied as a pre-filter for **all-pairs intersection cardinality**, where the popcount of the ANDed
summary is an exact work count rather than a heuristic. Claim that, and cite BitFunnel first.

---

## 3. The pairing matrix

Representations under consideration:

| Sym | Representation | Parameter | Best when |
|---|---|---|---|
| **B** | Dense bitmap | `m` words | both sides dense |
| **S** | Sorted scalar array (u16 within block / u32 global) | `\|A\|` = cardinality | very sparse |
| **R** | RLE / run container: `(start, length)` pairs | `r` = run count | clustered, few runs |
| **W** | WAH/EWAH: literal + fill words | literals + fills | long uniform runs |
| **Ro** | Roaring: per-chunk choice of {array, bitmap, run} | chunks | heterogeneous across the universe |

Symmetric cross-product ⇒ **15 distinct kernels**. Asymptotic cost of each, for
*cardinality only* (not materialization):

| Pairing | Cost | Notes |
|---|---|---|
| B × B | Θ(m) naive; **sub-linear with a zone map** (§2.6) | register blocking was tested on AArch64 and **does not pay** — the kernel is SIMD-issue bound, not load bound |
| **B × S** | **Θ(\|S\|)** | random access into bitmap; **high-value asymmetric cell** |
| **B × R** | **Θ(r)** | with a rank index — see §4. Independent of run *lengths* |
| B × W | Θ(literals + fills) | fills skip in O(1) with rank index |
| S × S | Θ(\|A\|+\|B\|) merge, or Θ(\|A\| log(\|B\|/\|A\|)) galloping | classic SIMD set intersection |
| S × R | Θ(\|S\| + r) or Θ(\|S\| log r) | binary search into runs |
| S × W | Θ(\|S\| + fills) | |
| R × R | Θ(r_A + r_B) | run merge |
| R × W | Θ(r_A + r_B) | |
| W × W | Θ(r_A + r_B) | |
| Ro × X | per-chunk dispatch | recursively decomposes into the above |
| Ro × Ro | Σ over shared chunks | CRoaring already implements the 3×3 sub-case |

**Every cell except B × B is sub-linear in `m`.** That is the entire point.

### 3.1 Selection granularity

The pairing decision can be made at three granularities, and all three are in scope:

1. **Per vector pair** — one decision per (i,j), from precomputed metadata.
2. **Per chunk within a pair** — Roaring-style, 2¹⁶-bit chunks, decision per shared chunk. Finer;
   necessary when a row is clustered (dense in one region, empty elsewhere) — which haplotype
   data emphatically is, because of linkage structure and population stratification.
3. **Per tile** — decision hoisted out of the inner loop by pre-partitioning rows so a whole tile
   shares one kernel. Amortizes the decision to near-zero. See §5.

### 3.2 Storage vs compute representation

These are separable and should not be conflated:

- What you **store** (memory-optimal, chosen once at ingest)
- What you **compute in** (speed-optimal for a given pairing, possibly materialized transiently)

It may be correct to store as Roaring and transiently expand a hot row to a dense bitmap because it
participates in thousands of dense pairings within a tile. The amortization threshold —
*how many pairings justify a transient conversion* — is itself a research question, and it is one
the all-pairs setting raises and the pairwise setting cannot.

---

## 4. Rank/select as the mechanism for run-heavy pairings

The strongest specific idea in this project.

For **cardinality only**, a run `[a, b)` on one side contributes exactly
`popcount(other_side[a..b))`. With a precomputed **prefix-popcount (rank) index** over the dense
side, that is answerable in **O(1) per run**:

```
contribution = rank_B(b) − rank_B(a)
```

Therefore **B × R costs Θ(r) — the number of runs — independent of how long those runs are.**
A row that is one 9-million-bit run of zeros and one 1-million-bit run of ones costs *two*
operations, not 156,250.

This is exactly the "99% empty ⇒ 99% of work eliminated" mechanism, made concrete and O(1) rather
than O(length).

Rank/select is a mature field — Jacobson, Clark, Vigna's `rank9`, the `sdsl` library — with
well-understood ~25% space overhead for constant-time rank. **It has not, as far as the prior-art
search found, been applied to all-pairs intersection cardinality against run containers.** That
cross-pollination is a genuine contribution and it is cheap to test.

Open questions: is the index worth building per-row or only for rows that participate in many
run-pairings? Block-granularity rank (coarse index + local popcount) trades space for a small
constant — where is that optimum here?

---

## 5. Why this is a different problem from Roaring

Roaring already does per-chunk adaptive representation, and CRoaring implements the 3×3
{array, bitmap, run} container pairing matrix. The novelty cannot be "adaptive representation."
It is four things Roaring does not address:

1. **The all-pairs setting.** Roaring optimizes *one* pairwise operation. At N² scale the
   per-chunk dispatch *branch itself* becomes a measurable cost, and the decision can instead be
   amortized across a tile by pre-partitioning rows. Roaring has no notion of this because it has
   no batch.

2. **Cardinality-only specialization.** Most Roaring container ops are built to materialize a
   result. Cardinality-only admits shortcuts that materialization forbids — above all the rank-index
   trick of §4, which computes a run's entire contribution without touching its bits.

3. **The asymmetric cells are underserved.** CRoaring's mixed-container operations are
   substantially scalar. **Verified against CRoaring `master`, 2026-08-04:**
   `bitset_container_and_justcard` is SIMD (AVX2/AVX-512/NEON), but
   `array_bitset_container_intersection_cardinality`,
   `run_bitset_container_intersection_cardinality` and
   `array_run_container_intersection_cardinality` are all scalar. The run×bitset path calls
   `bitset_lenrange_cardinality`, a per-run word-by-word popcount whose cost is proportional to run
   **length** — exactly what the rank index of §4 removes. The caveat that used to sit here is
   discharged.

   Note what this does *not* say: our own measurements (§2.5) show vectorizing these cells wins
   nothing. The gap is real but it is not a SIMD gap — it is a work-avoidance gap.

4. **A wider representation set.** Roaring's three containers are chunk-local by construction.
   WAH-style global fills and global sorted arrays behave differently and belong in the matrix.

---

## 6. Consequences for the research programme

This reframing reorders priorities relative to a pure-speed reading of the problem:

| | Mechanism | Regime | Magnitude |
|---|---|---|---|
| **Primary** | Right pairing chosen per pair/chunk | sparse and mixed pairs | **10²–10⁵×** |
| **Secondary** | **Work avoidance inside the asymmetric cells** — rank index, zone map, adaptive search strategy. *Not* vectorization: 0 of 25 measurement points are won by a SIMD kernel (§2.5) | mixed pairs | large — determines whether the primary win is realized |
| **Tertiary** | Dense-cell tuning. Register blocking **measured and refuted** on AArch64; what pays is accumulator count | dense × dense only | ~1.24–1.4× measured (the ~1.7× that used to sit here was an unsourced tier-1 estimate) |

Register blocking is still worth doing — it is the one cell where fixed cost is genuinely
unavoidable — but it must not be mistaken for the contribution. **Selection and asymmetric kernels
are the contribution.**

Two hard requirements follow:

- **Selection must be near-free.** At N² pairs, an expensive decision per pair eats the saving it
  was meant to unlock. Decisions must come from O(1) precomputed metadata (cardinality, run count,
  chunk occupancy bitmask) or be hoisted to tile granularity entirely.

- **Benchmarks must use realistic density distributions.** A uniform-density synthetic benchmark
  makes this entire contribution invisible, because it never generates the skew that motivates it.
  Benchmark data must include a 1/i-style frequency spectrum and clustered (linkage-like) structure.
  This is the single easiest way to accidentally benchmark away the result.

---

## 7. Falsifiable claims

| # | Claim | Falsified by |
|---|---|---|
| **P1** | On a realistic (1/i) frequency spectrum, adaptive pairing beats all-bitmap by ≥50× end-to-end | benchmark on generated + real haplotype-shaped data |
| **P2** | Per-pair selection can be made ≤2% of total runtime via O(1) metadata or tile hoisting | profile of the selection path at N² scale |
| **P3** | Vectorized asymmetric kernels (B×S, B×R, S×R) beat both scalar and inflate-to-bitmap fallback over a measurable density band | per-cell race across the density grid |
| **P4** | A rank index makes B×R cost Θ(runs), independent of run length, and pays for its space | B×R timing vs run count *and* run length, held separately |
| **P5** | The full 15-cell matrix beats Roaring's 3-container subset on skewed data | head-to-head vs CRoaring `and_cardinality` |

### 7.1 Evidence status (2026-08-04)

| # | Status | Evidence |
|---|---|---|
| **P1** | **HOLDS conditionally** | 55–91× over all-bitmap on run-structured data across four microarchitectures; 47–56× at density 2e-4 on three of four. Does **not** hold in the mid-density band, where bitmap × bitmap is genuinely correct. `results/CROSS_ISA.md` |
| **P2** | **REFUTED as measured** | Selection costs 28.3% of runtime against a 2% budget; oracle regret 94.1%. Gate 1 fails. The prescribed fallback (M3 tile hoisting) is unbuilt. `results/OPTLOG.md` |
| **P3** | **REFUTED as worded** | The asymmetric cells beat inflate-to-bitmap, but never by vectorizing: a SIMD kernel wins 0 of 25 corpus-cell points. `bs_neon_idx` and `br_neon` are correct and lose 0.51–0.86× |
| **P4** | **HOLDS** | Run count pinned, length varied 256×: no-index grows 18×, indexed shows no trend. Crossover measured at ~256-bit runs (the analytic estimate said 600–1000 and was wrong). `bench/p4_runlength.sh` |
| **P5** | **HOLDS, cross-ISA** | Beats a `run_optimize()`-tuned CRoaring at every density on all four hosts, 1.7–13.8×. Note the "15-cell matrix" of the original wording does not exist — Roaring cells were never built, only the 10 non-Ro cells |

P1 was called "close to arithmetic"; that was overconfident — it holds at the tails and fails in
the middle, which only measurement showed. The genuine research risk was correctly identified as
**P2**, and P2 is the one that failed.

---

## 8. Explicit non-goals

- **Applications.** No genomics tooling, no chemoinformatics integration, no vector-search layer.
  Haplotype data is the *motivating shape* and a benchmark generator target — not a product.
  Genome-wide LD is the **follow-on project** (reviving Tomahawk, `RESEARCH_PLAN.md` §8b), and it
  starts only after this one clears Gate 3. Storm stays domain-agnostic: no genotype encodings, no
  VCF/`.pgen` awareness, no r²/D′.
- **Approximate methods.** No MinHash, no LSH, no sketching. Exact cardinality only.
- **Pruning bounds** (Swamidass–Baldi). Real, and orthogonal: they change the complexity class by
  skipping pairs, whereas this work makes each pair cheap. A hook exists in the thresholded API;
  in scope only after the pairing matrix works.
- **Compression ratio.** Representations are selected for *speed of intersection*, not for size.
  Where the two conflict, speed wins; where storage matters, it is a reported trade-off, not an
  objective.

---

## 9. Relationship to the other documents

- **`LANDSCAPE.md`** — what already exists and who owns it. Note that its §7 was written before
  this reframing; the pairing matrix supersedes its ordering of contributions, though its
  prior-art findings stand unchanged.
- **`RESEARCH_PLAN.md`** — phases, gates, benchmark protocol, community mechanism, and the
  two-project sequence (§8b). Subordinate to this document; where they disagree, this document wins.
- **`AGENTS.md`** — working rules for anyone (human or agent) touching this repo.

**Programme order, fixed:** StormBitmaps (kernel paper) → Tomahawk (application paper). Never the
reverse, and never concurrently. `LANDSCAPE.md` §6 records what happened last time four repos were
started in one year.
