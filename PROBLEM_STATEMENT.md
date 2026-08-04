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
| B × B | Θ(m) | fixed; the only cell where register blocking matters |
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

3. **Vectorizing the asymmetric cells.** CRoaring's mixed-container operations are substantially
   scalar. B × S, B × R, S × R with real SIMD is largely unwritten — and by §2.3 these are the
   cells carrying most of the real-world benefit. *(Verify against current CRoaring source before
   claiming this in writing.)*

4. **A wider representation set.** Roaring's three containers are chunk-local by construction.
   WAH-style global fills and global sorted arrays behave differently and belong in the matrix.

---

## 6. Consequences for the research programme

This reframing reorders priorities relative to a pure-speed reading of the problem:

| | Mechanism | Regime | Magnitude |
|---|---|---|---|
| **Primary** | Right pairing chosen per pair/chunk | sparse and mixed pairs | **10²–10⁵×** |
| **Secondary** | Vectorized asymmetric kernels (B×S, B×R, S×R) | mixed pairs (~18% at p=0.9) | large — determines whether the primary win is realized |
| **Tertiary** | Register-blocked B×B microkernel | dense × dense only (~1% of pairs) | ~1.7× on a small slice |

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

P1 is close to arithmetic rather than a research question — §2 nearly proves it. The genuine
research risk is concentrated in **P2** (is selection cheap enough?) and **P3** (can the
asymmetric cells actually be vectorized well?). Those are the gates that matter.

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
