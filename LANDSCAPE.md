# StormBitmaps — Prior Art & Competitive Landscape

**Status:** assessment written 2026-08-04. Repo dormant since 2019-09-24.
**Purpose:** record what already exists, so any decision to resurrect, publish, or shelve this
work is made against evidence rather than memory.

**Provenance:** compiled from five parallel literature/software searches covering (1) SIMD set
intersection & compressed bitmap indexes, (2) binary/1-bit GEMM and BNN kernels, (3)
chemoinformatics fingerprint similarity, (4) genomics / linkage disequilibrium, (5) open-source
software landscape. Findings below are attributed; derived numbers are marked as such and
flagged where they need verification.

---

## 1. What this repo claims

`storm.c` / `storm.h` (~1300 lines C) plus the `libalgebra` submodule. Computes `XXᵀ` for a
binary matrix — equivalently, the all-pairs set-intersection cardinality `|Xᵢ ∩ Xⱼ|` for N sets.

Two storage models:

| Model | Layout | Intended regime |
|---|---|---|
| `STORM_contiguous_t` | Flat, 64-byte-aligned dense bitmaps, one contiguous buffer | N < ~256,000 |
| `STORM_t` | Roaring-like two level: block-id container → per-block dense bitmap **or** sorted 16-bit scalar array | Large / sparse |

Five techniques, referenced as (a)–(e) throughout this document:

- **(a)** AND + SIMD population count (SSE4.2 / AVX2 Harley-Seal / AVX-512BW / NEON)
- **(b)** Runtime CPU-feature dispatch (via `libalgebra`)
- **(c)** Cache-aware blocked tiling of the upper triangle
- **(d)** Density-adaptive three-way dispatch: bitmap×bitmap, bitmap×list, list×list
- **(e)** Roaring-like two-level container for large sparse inputs

Headline claims from `README.md`:
- ~0.209–0.21 cycles / 64-bit word, ~114 GB/s, on AVX-512BW, **for N < 256,000**
- ~0.43–0.48 cycles/word on large inputs, at rough parity with CRoaring
- Benchmarked on a 10 nm Cannon Lake Core i3-8121U — in 2019, essentially the only consumer
  part with AVX-512BW + VPOPCNTDQ

**Important caveat on the headline number.** 114 GB/s exceeds DRAM bandwidth on that machine.
The `N < 256,000` qualifier means the 0.21 cycles/word figure is a **cache-resident** measurement,
not a full-workload one. This is legitimate (all-pairs has O(N²M) work over O(NM) data, so a
well-tiled implementation *should* be compute-bound) but it is not comparable to
bandwidth-bound numbers quoted by other projects without normalization. Any future write-up
must state this explicitly or it will be attacked in review.

---

## 2. Verdict per technique

| Technique | Status | Closest prior art |
|---|---|---|
| (a) AND + SIMD popcount | **Published — by a coauthor of this repo** | Muła, Kurz & Lemire 2018 |
| (b) Runtime ISA dispatch | **Engineering folklore; shipped code pre-2019** | libpopcnt, CRoaring, simdjson |
| (c) Cache-blocked all-pairs tiling | **Published 2011** | Haque, Pande & Walters 2011 |
| (d) Density-adaptive dense/sparse dispatch | **Published** | EmptyHeaded 2016/2017; Ding & König 2011 |
| (e) Roaring-like two-level container | **Literally Roaring** | Lemire, Ssi-Yan-Kai & Kaser 2016 |

None of (a)–(e) is individually novel. The only gap all five searches converged on is that
**no single published work or shipped library packages all five as a general, domain-agnostic
binary `XXᵀ` primitive**. That is an integration gap, not an algorithmic one.

---

## 3. The two findings that dominate

### 3.1 Self-overlap: Tomahawk is prior art for this repo

`README.md` states it directly: *"These functions were originally developed for Tomahawk … for
computing genome-wide linkage-disequilibrium."*

Tomahawk's public **Algorithm Overview** page (github.com/mklarqvist/Tomahawk, repo created
2017-07-17; docs at mklarqvist.github.io/tomahawk) already documents:

- **Algorithm 2** — "transforms compressed RLE entries to uncompressed k-bit-vectors and
  use[s] machine-optimized SIMD-instructions" for O(N/W) horizontal comparison. This is
  technique (a) + the dense path.
- **Algorithm 3** — sparse non-reference positional index over 1-bit vectors,
  O(min(|NON_REF_A|, |NON_REF_B|)). This is the sparse path of technique (d).

StormBitmaps' first commit is 2019-03-09. The density-adaptive dispatch that would be the
natural thing to claim as this repo's architectural contribution was publicly documented
~1.5–2 years earlier, by the same author, in the tool this code was extracted from.

Related same-author repos, all sharing the `STORM_*` kernel prefix:

| Repo | Created | Last push | Stars | Note |
|---|---|---|---|---|
| `mklarqvist/Tomahawk` | 2017-07-17 | 2019-11-09 | 44 | Motivating application; **no paper or preprint found** |
| `mklarqvist/positional-popcount` | 2019-03-08 | — | — | One day before StormBitmaps |
| `mklarqvist/StormBitmaps` | 2019-03-09 | 2019-09-24 | 14 | This repo |
| `mklarqvist/libalgebra` | 2019-08-15 | 2019-12-16 | 45 | Factored-out kernel; created **after** StormBitmaps |

These are four packagings of one kernel from a single 2019 research push, not four independent
contributions. Any publication must say so explicitly.

**The one genuinely published piece of that push:**
Klarqvist, Muła & Lemire, *"Efficient Computation of Positional Population Counts Using SIMD
Instructions."* arXiv:1911.02696 (v1 2019-11-07, v6 2021-05-11); published in *Concurrency and
Computation: Practice and Experience* 33(17), 2021, DOI 10.1002/cpe.6304.
Reports <0.5 cycles/16-bit word, up to 400× fewer instructions, ~50× over scalar.

Note that **pospopcnt is a genuinely different operation** from Storm's kernel — a per-bit-position
histogram across a stream of words, not `popcount(A AND B)` between two words. The pospopcnt
paper never discusses set intersection, all-pairs computation, or LD. It is a legitimate
separate contribution and does *not* preempt Storm. It does, however, mean the "upcoming
paper" cited in `README.md` has been out for five years and covers something else.

### 3.2 Chemoinformatics got there first, repeatedly

This is the strongest prior-art domain by a wide margin. The field has treated all-pairs
AND+popcount as routine engineering since ~2007.

**Haque, Pande & Walters, "Anatomy of High-Performance 2D Similarity Calculations,"
*J. Chem. Inf. Model.* 51:2345–2351 (2011).**
https://pubs.acs.org/doi/10.1021/ci200235e · PDF: https://cs.stanford.edu/people/ihaque/papers/2dtanimoto.pdf

The single closest precedent. Computes the full all-vs-all Tanimoto matrix using SSE4 POPCNT
(with SSSE3/PSHUFB and log-time bit-reduction fallbacks for older hardware — i.e. an early form
of (b)), and **explicitly uses Morton / Z-order tiling of the similarity matrix so that the
fingerprints for a tile fit in cache** — technique (c), eight years earlier. Reports 174–217M
Tanimoto/sec on SSE4+Morton for 1024-bit fingerprints over 32K–131K molecule matrices; GTX 480
GPU at 1.09–1.16 billion/sec.

*Derived comparison (verify before citing):* 217M cmp/s × 16 words/cmp ≈ 3.5G words/s; against a
4-core 2.5 GHz CPU (~10G cycles/s aggregate) ≈ **2.9 cycles/word**, roughly 14× slower than
Storm's claimed 0.2. Most of that gap is ISA and hardware generation (SSE4 POPCNT on 2011
silicon vs AVX-512BW VPOPCNTDQ), not a new algorithmic idea. No density-adaptive sparse
switching — dense-only throughout. That absence is the one real difference.

**Dalke, "The chemfp project," *J. Cheminformatics* 11:76 (2019).**
https://link.springer.com/article/10.1186/s13321-019-0398-8 · PMC6896769

Preempts most of the mechanical core independently. Ships: SIMD popcount with an AVX2
Harley-Seal path faster than hardware POPCNT for 1024/2048-bit words ("10.6–11.0× speedup
relative to 8-bit lookup tables"); runtime dispatch across popcount methods; 64-byte alignment
"for possibly better AVX2/AVX512 performance"; **full N×N all-vs-all — "The N×N threshold search
computes the upper triangle in parallel then uses a single thread to fill in the lower
triangle"**; popcount-sorting for memory coherency; and BitBound pruning (Swamidass–Baldi).

Measured: **130M 1024-bit Tanimotos/sec single-core** (7.4 ns/Tanimoto with AVX2); 1M-fingerprint
N×N matrix in ~30 min on 4 OpenMP threads; achieves **16 GiB/s of 25 GiB/s theoretical memory
bandwidth**.

What chemfp does *not* have: AVX-512BW (only aligns memory "for possibly better" use of it), and
a density-adaptive representation switch — it defers to inverted-index methods on very sparse
fingerprints rather than switching internally. It also has a **pruning layer Storm lacks
entirely**: Storm computes every pairwise cardinality exactly and unconditionally over the full
upper triangle.

**Swamidass & Baldi, "Bounds and Algorithms for Fast Exact Searches of Chemical Fingerprints in
Linear and Sublinear Time," *JCIM* 47:302–317 (2007).**
https://pubs.acs.org/doi/10.1021/ci600358f
Establishes the popcount-cardinality bound: given |X| and |Y|, Tanimoto is bounded without
computing the intersection at all. This prunes work *before* any AND+popcount runs. Storm has
no equivalent — a structural weakness for any thresholded workload, and the first thing a
reviewer from this field would ask about.

**Vachery & Ranu, "RISC: Rapid Inverted-Index Based Search of Chemical Fingerprints,"
*JCIM* 59 (2019).** https://pubs.acs.org/doi/10.1021/acs.jcim.9b00069
The field's actual sparse/dense boundary work. Sparse inverted indices + pruning for unfolded
high-dimension fingerprints (2048+ bits, ECFP density ~0.024): "at fingerprints of dimension 2048
and above, RISC is consistently faster than the state-of-the-art" including chemfp/BitBound,
while chemfp/BitBound wins above density ~0.1. **This is the direct chemoinformatics analogue of
technique (d) — but presented as two competing tools to choose between at design time, not as
one library that dispatches at runtime.** That framing difference is the narrow opening
discussed in §7.

Also relevant: SIML (Haque & Pande, *JCIM* 50:560–564, 2010) — SIMD chemical similarity, different
metric (LINGO substring), establishes the research line predates Storm by a decade. Ma et al.,
"GPU Accelerated Chemical Similarity Calculation for Compound Library Comparison," *JCIM* (2011),
PMC3445263 — explicit all-vs-all Tanimoto matrix, 32M PubChem × 10K probes in ~20 min.

---

## 4. Prior art by domain

### 4.1 SIMD set intersection & compressed bitmaps

**Muła, Kurz & Lemire, "Faster Population Counts Using AVX2 Instructions,"
*The Computer Journal* 61(1):111–120, 2018 (arXiv:1611.07612, 2016).**
https://arxiv.org/abs/1611.07612

Preempts technique (a) in full. **§4 "Beyond population counts" explicitly computes
`popcount(A AND B)` for Jaccard-index set-intersection cardinality** — the exact operation Storm
performs — presents the Harley-Seal/CSA vectorized popcount (Figs. 6/12), and reports in Table 4:

| Kernel | Cycles per 64-bit word pair |
|---|---|
| Scalar POPCNT, AND + count | 2.76–2.78 |
| AVX2 Harley-Seal, AND + count | **1.15** (≈0.575 cycles/word) |

with the explicit observation that *"using AVX2 for both the Boolean operation and both
population counts gives us the Boolean operation almost for free."*

For pure popcount on Haswell (i7-4770, 3.4 GHz), large arrays: scalar POPCNT 1.01 c/word,
AVX2 Muła shuffle 0.69, AVX2 Harley-Seal 0.52.

This paper is co-authored by Daniel Lemire, who is credited as a collaborator on StormBitmaps.
It does not cover AVX-512/VPOPCNTDQ (which postdates it), runtime dispatch, cache-blocked
all-pairs iteration, or dense/sparse hybrid — but it owns the kernel.

**Lemire, Ssi-Yan-Kai & Kaser, "Consistently faster and smaller compressed bitmaps with Roaring,"
*Software: Practice and Experience* 46(11):1547–1569, 2016 (arXiv:1603.06549).**
https://arxiv.org/abs/1603.06549
Technique (e) verbatim: 16-bit block id + dense bitmap or sorted array per block, with automatic
array↔bitmap conversion at a 4096-element threshold per 65536-block. Also the density-adaptive
half of (d). Does not cover all-pairs, triangular tiling, or AVX-512 internals.

**Chambi, Lemire, Kaser & Godin, "Better bitmap performance with Roaring bitmaps,"
*SPE* 46(5), 2016 (arXiv:1402.6407).** https://arxiv.org/abs/1402.6407
Earlier Roaring paper; intersections 4–5× faster than WAH/Concise.

**Lemire et al., "Roaring Bitmaps: Implementation of an Optimized Software Library,"
*SPE*, 2018 (arXiv:1709.07821).** https://arxiv.org/abs/1709.07821
Adds run containers and `roaring_bitmap_and_cardinality()`-style routines that compute
intersection cardinality **without materializing the AND result** (~2× faster than
materialize-then-count per library docs). Preempts "cardinality without materialization" for the
pairwise case. Not batched/all-pairs.

**Aberger, Tu, Olukotun & Ré, "EmptyHeaded: A Relational Engine for Graph Processing,"
SIGMOD 2016 / *ACM TODS* 42(4), 2017 (arXiv:1503.02368).** https://arxiv.org/abs/1503.02368
The strongest published instance of technique (d). An optimizer picks between a `uint`
(sparse list) and a `bitset` (dense) layout **per relation, based on density**, *and* selects the
intersection algorithm (SIMD galloping vs SIMD shuffling) based on the cardinality ratio between
operands. Functionally the same idea as Storm's three-way dispatch, three years earlier. Does not
target all-pairs N×N cardinality, triangular tiling, or a Roaring-style container — it is a
relational engine, not a kernel.

**Ding & König, "Fast Set Intersection in Memory," *PVLDB* 4(4):255–266, 2011
(arXiv:1103.2409).** https://arxiv.org/abs/1103.2409
Adaptive, input-size-aware algorithm selection (galloping vs merge by relative set size/skew) —
the general principle behind (d), for sorted lists, no SIMD, no bitmaps.

**Schlegel, Willhalm & Lehner, "Fast Sorted-Set Intersection using SIMD Instructions,"
ADMS @ VLDB 2011.** https://www.adms-conf.org/p1-SCHLEGEL.pdf
SSE4.2 `PCMPESTRM`-based sorted-list intersection. Sorted-integer representation, not
bitmap AND+popcount.

**Inoue, Ohara & Taura, "Faster Set Intersection with SIMD Instructions by Reducing Branch
Mispredictions," *PVLDB* 8(3):293–304, 2014.** http://www.vldb.org/pvldb/vol8/p293-inoue.pdf
SIMD merge-based sorted-array intersection (the "SIMD Galloping"/BMiss line). Materializes
elements; irrelevant to cardinality-only queries.

**Han, Zou & Yu, "Speeding Up Set Intersections in Graph Algorithms using SIMD Instructions"
(QFilter), SIGMOD 2018.** https://dl.acm.org/doi/10.1145/3183713.3196924
Extends the above to graph adjacency lists / triangle counting — conceptually adjacent to the
all-pairs use case, but sorted-list based.

**Wu, Otoo & Shoshani, "Optimizing Bitmap Indices with Efficient Compression," *ACM TODS* 31(1),
2006 (WAH).** https://dl.acm.org/doi/10.1145/1132863.1132864
Word-aligned hybrid run/literal compressed bitmaps — ancestor of EWAH/Concise/Roaring. Scalar,
word-at-a-time.

**Colantonio & Di Pietro, "Concise: Compressed 'n' Composable Integer Set,"
*Information Processing Letters* 110(16), 2010.**

### 4.2 Binary / 1-bit GEMM

The "binary dot product = XNOR/AND then popcount" primitive was canonized 2015–2016 and
independently re-optimized many times before 2019.

- **Courbariaux, Hubara, Soudry, El-Yaniv & Bengio, "Binarized Neural Networks," NeurIPS 2016**
  (arXiv:1602.02830), building on BinaryConnect (NeurIPS 2015). Establishes the identity and
  notes it "translates directly into single assembly commands."
- **Rastegari, Ordonez, Redmon & Farhadi, "XNOR-Net," ECCV 2016** (arXiv:1603.05279). ~58× CPU
  speedup claim for raw convolution.
- **Hubara et al., "Quantized Neural Networks," JMLR 2017** (arXiv:1609.07061). Bit-serial GEMM.
- **Yang et al., "BMXNet," ACM MM 2017** (arXiv:1705.09864). XOR + `__builtin_popcount`, ~13×
  over ATLAS CBLAS at large batch. The paper itself concedes it is "significantly slower than
  what can be achieved with optimised assembly kernels."
- **Zhang et al., "daBNN," ACM MM 2019** (arXiv:1908.05858). ARM NEON assembly, 7–23× over BMXNet.
- **Bannink et al., "Larq Compute Engine," MLSys 2021** (arXiv:2011.09398). 8.5–18.5× over
  full-precision on mobile.
- **Umuroglu & Jahre, "Streamlined Deployment for Quantized Neural Networks,"
  arXiv:1709.04060 (2017).** Bit-serial AND/popcount matmul, 3.5× over optimized 8-bit.
- **Umuroglu et al., BISMO, FPL 2018 / ACM TRETS 2019** (arXiv:1806.08862, 1901.00370).
  FPGA, 6.5–15.4 TOPS.
- **Pedersoli et al., "Espresso," arXiv:1705.07175 (2017).** CPU+GPU binary GEMM library, 68× over
  BinaryNet.
- **NVIDIA CUTLASS 1-bit BMMA / Zhang et al., arXiv:2006.16578 (2020).** Turing tensor cores
  execute `wmma.mma.xor.popc…b1.b1` natively (dot = K − 2·popc(A⊕B)); >12× over FP16 cuBLAS at
  4K on RTX 2080.
  ⚠ **Verify before relying on this:** 1-bit and INT4 tensor-core paths appear to have been
  deprecated in later NVIDIA architectures. Confirm current Blackwell/Hopper status before
  building any GPU comparison or claiming a GPU gap.

**Not applicable — Method of Four Russians.**
Albrecht, Bard & Hart, "Algorithm 898: Efficient Multiplication of Dense Matrices over GF(2),"
*ACM TOMS* 2010 (arXiv:0811.1714); M4RI library. M4RM precomputes all 2^k linear combinations of
k rows to get O(n³/log n) for GF(2) products. **This does not transfer.** M4RI computes the GF(2)
*sum* (parity — a single output bit per entry); Storm needs the exact integer cardinality
`popcount(Xᵢ AND Xⱼ)`. The table-reuse trick amortizes bitwise combination but gives no shortcut
for counting set bits. Distinct problem despite the surface similarity. Worth stating explicitly
in any write-up, because reviewers will raise it.

**Blocking theory.** Goto & van de Geijn, "Anatomy of High-Performance Matrix Multiplication,"
*ACM TOMS* 2008; Van Zee & van de Geijn, BLIS, *ACM TOMS* 2015. Technique (c) is a textbook
instance of this — a popcount kernel substituted for an FMA kernel. No new blocking theory here.

**All-pairs Hamming.** Grabowski, "A fast algorithm for all-pairs Hamming distances,"
*IPL* 2018; and "Algorithms for all-pairs Hamming distance based similarity," *SPE* 2021.
Directly addresses all-pairs binary similarity with SIMD popcount (phylogenetics motivation), but
built around threshold-based early termination/filtering rather than computing the full matrix.

### 4.3 Genomics / linkage disequilibrium

- **Chang, Chow, Tellier, Vattikuti, Purcell & Lee, "Second-generation PLINK," *GigaScience* 4:7
  (2015).** DOI 10.1186/s13742-015-0047-8, arXiv:1410.4803. Preempts the core mechanism in this
  domain: 2-bit-packed genotype vectors, SSE2 popcount-based bit-parallel `--r2` (LD) and
  `--genome` (all-pairs IBS) across every pair of individuals, **processed in 960-marker blocks**
  (i.e. blocked tiling). Explicitly flags future "deviations from most common value" sparse
  storage for low-MAF variants — foreshadowing (d), four years early.
- **PLINK 2.0** (cog-genomics.org; github.com/chrchang/plink-ng). Shipped the sparse-variant
  storage PLINK 1.9 only proposed, plus vectorized SSE2/AVX2 popcount kernels in
  `pgenlib`/`plink2_ld`. Directly overlaps the (d) novelty claim.
- **Zheng, Levine, Shen, Gogarten, Laurie & Weir, "SNPRelate," *Bioinformatics* 28(24):3326–3328
  (2012).** DOI 10.1093/bioinformatics/bts610. SSE2-accelerated all-pairs IBS/IBD/GRM matrices —
  an "all-pairs XXᵀ via bit tricks" implementation, seven years before Storm.
- **Quick, Fuchsberger, Taliun, Abecasis, Boehnke & Kang, "emeraLD," *Bioinformatics*
  35(1):164–166 (2019).** DOI 10.1093/bioinformatics/bty547. Exploits sparsity/haplotype structure
  for order-of-magnitude faster LD at biobank scale — the same "sparse beats dense at low density"
  intuition, same year.
- **Sapin & Keller, *Bioinformatics* (2021)**, bioRxiv 2020.07.07.191999. Tiling O(n²) genomic
  comparisons to 435K UK Biobank individuals via affine-plane decomposition. Evidence that the
  all-pairs tiling problem was being independently attacked.
- **Joubert, Nance, Weighill & Jacobson, arXiv:1705.08210 (2017).** GPU/Xeon-Phi all-pairs
  genomic similarity for GWAS/PheWAS scale.
- **Purcell et al., PLINK 1.07 (2007).** Origin of the popcount-based bit-parallel genotype
  lineage — ~12 years old by Storm's release.
- **Context:** Myers (1999) bit-vector edit distance; Baeza-Yates & Gonnet (1992) Shift-Or.
  Bit-parallelism is a deeply established idiom throughout bioinformatics; "apply bitwise tricks
  for speed" is not a novel framing in this field.

### 4.4 All-pairs similarity search (the alternative framing)

**Bayardo, Ma & Srikant, "Scaling Up All Pairs Similarity Search," WWW 2007, pp. 131–140.**
https://dl.acm.org/doi/10.1145/1242572.1242591
Canonical APSS: inverted index + prefix filtering to **avoid** the O(N²) computation entirely for
sparse high-dimensional data above a similarity threshold.

This matters strategically. The APSS/IR community's answer to "all pairs similarity" is
indexing and pruning, not faster brute force. Storm accelerates the constant factor of a
quadratic algorithm that this literature argues should be avoided. Any paper must justify why
exact, unpruned, full-matrix computation is the right target — the honest answer being: when you
genuinely need every entry (clustering, full LD matrices, distance-matrix construction), not
when you need top-k or a threshold.

Also: Johnson, Douze & Jégou, "Billion-Scale Similarity Search with GPUs," *IEEE TBD* 7:3 (2019),
arXiv:1702.08734 (FAISS) — approximate, dense float, but it is where large-scale similarity
infrastructure actually went.

---

## 5. Software landscape

Stars/activity as of 2026-08.

| Project | URL | Stars | Last activity | Overlap / gap |
|---|---|---|---|---|
| **CRoaring** | RoaringBitmap/CRoaring | ~1,870 | Active (2026-08-03) | Has `roaring_bitmap_and_cardinality`, SIMD AVX2/AVX-512/NEON, adaptive array/bitset/run containers. **No batched all-pairs API** — callers write their own O(N²) loop. No triangular tiling, no bitmap×list mixed comparator |
| **BitMagic** | tlk00/BitMagic | ~454 | Active (2026-08-01) | Closest architectural cousin. `bm::aggregator<>` does vectorized AND/OR/AND-MINUS across *groups* of bit-vectors, plus similarity/distance helpers and SSE4.2/AVX2/AVX-512 popcount. But: **compile-time SIMD selection only** (README states it does *not* do runtime CPU identification), and it is fan-in aggregation ("AND many into one"), not "AND every pair, record N² scalars" |
| **libpopcnt** | kimwalisch/libpopcnt | ~369 | Active (2026-07-23) | Runtime CPUID dispatch (AVX512-VPOPCNT/AVX2/POPCNT/NEON/SVE) — technique (b) as a shipped library. Single-array popcount only; no AND, no pairwise |
| **sse-popcount** | WojciechMula/sse-popcount | ~357 | Stale (2024-04) | Reference/benchmark implementations of SIMD popcount kernels. Both Storm and FPSim2 descend from this |
| **SimSIMD / JaccardIndex** | ashvardanian/JaccardIndex | ~22 | 2025-05 | Hand-tuned AVX2/AVX-512 (VPOPCNTQ/VPSHUFB/VPSADBW/VPDPBUSDS) Jaccard kernels for fixed-width vectors (256/1024/1536-bit), aimed at vector search. **Pairwise only** — no batching, no matrix, no density adaptivity. Note the sibling `SimSIMD` project is far more visible and is the live competitor in the vector-search framing |
| **EWAHBoolArray** | lemire/EWAHBoolArray | ~459 | Semi-active (2024-10) | RLE-compressed bitmap AND/OR, pairwise on compressed streams. No SIMD popcount fusion, no matrix |
| **simdcomp / SIMDCompressionAndIntersection** | lemire/* | ~534 / ~446 | Active (2026-06) | SIMD sorted-list intersection — the list×list half of (d). Materializes rather than counting; no bitmap side, no matrix |
| **roaring-rs / croaring-rs** | RoaringBitmap/* | ~948 / ~171 | Active | Rust ports/bindings; same pairwise-only limitation |
| **hi_sparse_bitset** | tower120/hi_sparse_bitset | ~66 | Active (2026-05) | Hierarchical sparse bitset, lazy inter-set AND/OR iterators — conceptually similar to (e). Pure Rust, no SIMD intrinsics, no all-pairs |
| **Pilosa / FeatureBase** | FeatureBaseDB/featurebase | ~2,524 | Stale (2024-02) | Roaring-based database for row×column intersections. Query engine, not a kernel library |
| **chemfp** | chemfp.com | n/a | Active | Fast core is **closed/commercial** (BSD Python wrapper only). Dense bitmap×bitmap at ~7.4 ns/1024-bit pair with AVX2, BitBound pruning. One-vs-many search plus N×N mode; no sparse-list mode |
| **FPSim2** | chembl/FPSim2 | ~177 | Active (2026-07-16) | Vendors Muła's SIMD popcount for Tanimoto/Tversky. One-vs-many; no adaptive container, no tiled triangle, no runtime dispatch |
| **RDKit** | rdkit/rdkit | ~3,537 | Very active | `BulkTanimotoSimilarity` over `ExplicitBitVect`, dense AND+popcount, portable/scalar popcount, no documented AVX dispatch, no all-pairs matrix API |
| **OpenBabel FastSearch** | openbabel/openbabel | ~1,361 | Active (2026-07-11) | Folded bitset + popcount index, one-vs-many only, no SIMD intrinsics of note |
| **Larq Compute Engine** | larq/compute-engine | ~256 | Active (2026-07-30) | Nearest binary-GEMM analog: bitpack + XNOR + popcount, hand-tuned per-ISA. NN inference (im2col conv), not set semantics, no triangular specialization, TFLite-coupled |
| **daBNN** | JDAI-CV/dabnn | ~774 | Dead (2019-11) | ARM assembly binary GEMM. Same vintage as Storm, same fate |
| **BMXNet** | hpi-xnor/BMXNet | ~351 | Dead (2019-11) | XOR + `__builtin_popcount` inside MXNet |
| **PLINK 2.0** | chrchang/plink-ng | ~509 | Active (2026-07-27) | SIMD popcount + cumulative-popcount tables for genotype LD. Domain-specific (2-bit genotype codes, r²), not a general binary-matrix library, no bitmap/list density switch |
| **Tomahawk** | mklarqvist/tomahawk | ~44 | Dead (2019-11-09) | Same author; the tool this repo was extracted from. See §3.1 |

Ruled out as competitors on inspection: Judy arrays, Sux/SDSL (succinct rank/select, not
similarity engines), FastBitSet.js (JS, single bitset), awesome-simd (a list, not code).

**Does anything ship a batched all-pairs intersection-cardinality API with density-adaptive
dispatch?** No. Verified across all of the above. The individual ingredients all pre-exist —
mostly in Lemire's own repos, unsurprising given `libalgebra`'s lineage — but the combination as a
single general-purpose package does not.

---

## 6. This repo's adoption

- **14 stars, 3 forks, 0 open issues.** Created 2019-03-09, last push 2019-09-24 — ~7 years dormant.
- **All 3 forks are non-substantive.** `Zabrane/StormBitmaps` and `clayne/StormBitmaps` are known
  bulk-archival forkers; `CreRecombinase/StormBitmaps` is byte-identical to upstream
  (`ahead_by: 0, behind_by: 0`). No downstream code exists in any of them.
- **No third-party vendoring or citation found.** GitHub code search for "StormBitmaps" returns
  20 hits; on inspection only 2 are genuine — this repo's own `README.md`/`appveyor.yml`, and a
  self-citation in `mklarqvist/phd-thesis` Chapter 2. The other ~18 are false positives
  (unrelated Ruby weather scripts in a Pokémon fangame).
- **One genuine external mention:** listed in the community-curated `awesome-simd/awesome-simd`,
  citing the ~114 GB/s / ~14 billion bitmaps/sec claim and the runtime ISA selection design.
- Sibling repos went dormant simultaneously (`libalgebra` 2019-12-16, `Tomahawk` 2019-11-09),
  consistent with a single 2019 research push that was not sustained.

---

## 7. Where a contribution could still live

Nothing in (a)–(e) is defensible as novel. Scope is **kernel and algorithm level only** — no
application layer, no domain integration. Five things are unclaimed, in decreasing order of
strength:

**7.1 Register-blocked multi-output popcount microkernel.**
This is the largest unexploited win in the repo and appears nowhere in the literature.
`STORM_wrapper_diag_blocked` (`storm.c:222`) and `STORM_wrapper_diag_list_blocked` (`storm.c:282`)
implement *cache*-level blocking, but the innermost loop still invokes `f(left, right, n_ints)`
one pair at a time. That is a **pairwise** kernel called in a loop — each invocation re-streams
both operands, giving a 2:1 load-to-popcount ratio and leaving the kernel load-port- or
bandwidth-bound rather than VPOPCNTQ-bound.

Goto & van de Geijn's actual contribution was not cache blocking alone but the packed
**microkernel with register blocking**. The popcount analogue does not exist: hold one left
vector in a register and stream K right vectors against it, accumulating into K separate
registers, so each loaded line is reused K times. An 8-wide blocking takes the load:popcount
ratio from 2:1 to ~1:1; blocking both dimensions (4×4 = 16 outputs from 8 loads) takes it to
~0.5:1.

Muła/Kurz/Lemire optimized the *pairwise* kernel; Haque et al. and chemfp did *cache* tiling;
nobody has written the register-blocked multi-output popcount microkernel. It is the obvious
route to the ~1.7× headroom between the measured 0.21 and the estimated ~0.125 cycles/word
ceiling, and it is a pure kernel contribution.

**7.2 A SIMD kernel for the mixed bitmap×list case.**
The bitmap↔scalar path is currently a scalar loop — and it is the one carrying the precedence bug
(§8), so it has almost certainly never been correctly measured. No published work provides a
*vectorized* kernel for dense-bitmap-against-sorted-sparse-list intersection cardinality: the
SIMD set-intersection literature (Schlegel, Inoue, Han/QFilter, Lemire's
`SIMDCompressionAndIntersection`) covers list×list; the popcount literature covers bitmap×bitmap.
The mixed case is the seam between the two bodies of work and nobody has written it.

Concrete angles: gather-based word fetch (`VPGATHERDD`) over `list[i] >> 6`; exploiting the fact
that a sorted list has consecutive values frequently sharing a 64-bit word, so runs can be
collapsed into a single load plus a mask-build; or `VPCONFLICTD`-based deduplication of word
indices. This is a genuinely open, well-scoped kernel problem, and it is precisely the path that
technique (d) depends on.

**7.3 A predictive cost model for the density crossover.**
The current thresholds are hardcoded magic numbers: `scalar_cutoff = min(vector_length/200, 200)`
(`storm.c:1016`), `STORM_DEFAULT_SCALAR_THRESHOLD 4096` (`storm.h:46`). Nobody has published a
*calibrated, portable, derived* model for when bitmap×bitmap beats bitmap×list beats list×list,
parameterized by density, vector length, cache level, and ISA. EmptyHeaded did this for relational
joins; RISC vs chemfp established the crossover exists in chemoinformatics but treats it as a
design-time choice between separate tools. A validated model across AVX-512 / SVE2 / NEON is a
real, small, defensible contribution.

**7.4 Density-sorted scheduling for all-pairs.**
The dense/sparse decision is currently per-pair, which means branching inside the hot loop.
Sorting or partitioning input vectors by density so that dense-dense, dense-sparse, and
sparse-sparse pairs form *contiguous homogeneous tiles* eliminates the branch entirely and lets
each tile run a specialized kernel. chemfp popcount-sorts for memory coherency, but for a
different reason (pruning bounds), and does not partition the schedule by kernel. This is a
genuine scheduling idea specific to the all-pairs setting.

**7.5 An honest roofline for all-pairs binary operations.**
Where the computation is ALU-bound vs bandwidth-bound as a function of N and M, and why the
114 GB/s number is cache-resident. Useful and clarifying — strong blog material, weak paper
material on its own.

*Derived estimate, needs verification on real hardware:* AVX-512 VPOPCNTQ has ~1/cycle throughput
on Ice Lake / Sapphire Rapids / Zen 4 (port-limited); one 512-bit vector = 8 words, giving a
**~0.125 cycles/word ceiling** for AND+popcount+accumulate. The 2019 measurement of 0.21 is
therefore ~60% of peak, implying ~1.7× headroom. Zen 5's full-width 512-bit datapath may raise
this further. Note also that with native VPOPCNTQ, the Harley-Seal CSA trick is **no longer a
win** — it exists to reduce popcount instruction count on ISAs lacking a vector popcount, and
should be dropped from the AVX-512 path.

**What has changed since 2019 that helps:** AVX-512BW + VPOPCNTDQ was, in 2019, essentially
exclusive to the one odd Cannon Lake part this repo benchmarked on. It is now mainstream —
Ice Lake+, Sapphire Rapids, Zen 4 (2022), Zen 5 (2024) — plus ARM SVE2 on Neoverse V2 /
Graviton 3–4. The hardware this design was built for finally exists everywhere.

**Note on scope.** Under a kernel-only remit, the application-level moats of the incumbents
(chemfp's BitBound pruning and ecosystem, PLINK2's `.pgen`/MAF/missing-genotype handling) are not
competitive obstacles — a kernel library is the thing such tools *call*, not a rival to them. The
correct comparison set narrows to CRoaring's `and_cardinality` in a loop, `libpopcnt`, SimSIMD's
pairwise Jaccard kernels, and the published kernel numbers in Muła/Kurz/Lemire Table 4. The
corresponding cost is that a kernel library with no application has no organic adoption path —
this repo's 14 stars and zero downstream use over seven years is direct evidence. If the goal is a
publishable contribution plus a clean artifact rather than users, that trade is acceptable, and it
is exactly the register Lemire's own repos occupy.

**Venue fit.** Kernel-only is the *right* register for this material, not a limitation. The
closest prior art — Muła/Kurz/Lemire, *The Computer Journal* 2018 — is a pure kernel paper with no
application. So are the Roaring papers in *SPE*. A paper built on 7.1–7.4 would sit in exactly the
same literature it cites, which is the strongest position available here.

---

## 8. Defects — all fixed in Phase 0 (2026-08-04)

The repo did not build on any current toolchain and did not build *at all* on arm64. There was no
test suite. Both are now addressed: `tests/test_storm.c` provides 1,279 checks against an
independent oracle, wired into CTest.

Every fix below was verified by **reverting it and confirming the tests fail** — a test that cannot
fail is worthless, and the first version of this suite caught none of these (see §8.2).

### 8.1 Correctness

| Location | Defect | Verified by |
|---|---|---|
| `storm.c` `STORM_intersect_bitmaps_scalar_list` | `#define MOD(x) (((x)*64)>>6)` is the **identity**, not a modulo, leaving the shift count unmasked — UB for positions ≥ 64. Literal was `1L`, 32-bit on LLP64/Windows. **This is the B×S kernel, the project's top-priority cell.** | UBSan: *"shift exponent 76 is too large for 64-bit type"*. Not behaviourally observable on x86-64/AArch64 (both mask shifts to 6 bits, which coincides with the intent) — but real UB, and a genuine Windows bug |
| `storm.c:594`, `:602`, `:636`, `:644` | `data[x] & (1ULL << y) != 0` parses as `data[x] & ((1ULL<<y) != 0)` → `data[x] & 1`. **All four bitmap↔scalar paths tested only bit 0** | Regression test (dense/scalar mixed `STORM_t`) fails when reverted |
| `storm.c` `STORM_contig_add` | `memcpy(scalar, old, tot_scalar)` missing `* sizeof(uint32_t)` — copied ¼ of the data on scalar-buffer growth | Reverting causes **SIGSEGV** in the realloc test |
| `storm.c` `STORM_contig_add` (×2) | `n_scalar[j]` indexed by scalar-buffer offset instead of vector index `i`; the second site also read `n_scalar[i]` for `i ≥ n_data`, which is uninitialised | 4 failing checks when reverted |
| `storm.c` `STORM_contig_add` | Deduplicating scalar copy wrote at the **source** index while skipping duplicates, leaving uninitialised gaps and overrunning the reserved region | 2 failing checks when reverted |
| `storm.c:975` | `STORM_intersect_cardinality_square` was **declared but never defined** — any caller failed to link. Now implemented | Links |
| `storm.c` `STORM_free`, `STORM_contig_free` | Neither freed the struct allocated by its `_new()`; nested container/bitmap buffers were never released (the loops were commented out because the existing `_free` helpers call `free()` on mid-array pointers). Added members-only helpers | macOS `leaks`: **0 leaks** fixed vs **9 leaks / 768 bytes** reverted |

### 8.2 Portability — the repo did not build on arm64

| Location | Defect |
|---|---|
| `libalgebra/libalgebra.h` | `#include <x86intrin.h>` guarded only by `#ifndef _MSC_VER` — a *compiler* test, not an *architecture* test. Any non-x86 target with a non-MSVC compiler failed to compile. Also relied on `x86intrin.h` to transitively supply `posix_memalign`/`free`; now includes `<stdlib.h>` directly. **This is in the submodule and needs upstreaming to `mklarqvist/libalgebra` + a pin bump** |
| `storm.c` `STORM_intersect_vector16_cardinality` | The SSE4.2 S×S kernel was entirely unguarded. Now behind `STORM_HAVE_SSE42`; the pre-existing scalar tail doubles as the portable fallback |
| `benchmark.cpp:36` | `get_cpu_cycles()` was raw x86 RDTSC via encoded bytes. Added `__i386__`, `__aarch64__` (CNTVCT_EL0) and `std::chrono` paths |

### 8.3 Build

| Location | Defect |
|---|---|
| `CMakeLists.txt:1` | `cmake_minimum_required(VERSION 2.8)` — CMake ≥ 4.0 **refuses to configure**. Bumped to 3.15 |
| `CMakeLists.txt` | `STORM_ENABLE_SIMD_AVX512` passed `-mavx512`, which is not a valid GCC/Clang flag — the option could never have worked. Now `-mavx512f -mavx512bw -mavx512vl -mavx512vpopcntdq` |
| `CMakeLists.txt` | `find_path(... NAMES REQUIRED roaring/roaring.h)` mis-uses the `NAMES` list and made CRoaring a **hard requirement for the entire project**, including the library and tests that do not use it. Now optional; only `benchmark` needs it |
| `CMakeLists.txt` | `CMAKE_C_STANDARD 99` is inherited by `add_subdirectory()` and breaks CRoaring's C11 `<stdatomic.h>` detection (`#error "Unknown atomic implementation"`). Now C11 |
| repo-wide | CRoaring was unpinned — whatever happened to be installed. Now vendored as a submodule at **v4.7.2**, so baseline numbers are reproducible and version-stated |
| repo-wide | `.travis.yml` / `appveyor.yml` target defunct services — **still outstanding**, needs GitHub Actions |

### 8.4 Measurement caveat, newly surfaced

`get_cpu_cycles()` is **not a core cycle counter on any target**. x86 RDTSC is a constant-rate
reference clock, so it equals core cycles only at the nominal frequency — not under turbo and not
under AVX-512 downclocking, which is exactly the regime of interest. AArch64 `CNTVCT_EL0` is a
~24 MHz system timer on Apple silicon, two orders of magnitude off the core clock.

**Every historical "cycles/word" number in `README.md` was produced by this counter.** They are
reference ticks per word, not cycles per word. This compounds the tier-1 problem recorded in
`AGENTS.md`: the headline metric needs `perf` (Linux) or kperf/Instruments (macOS) before any of
it is citable.

---

## 9. Bottom line

Every individual technique in this repo has direct published or shipped precedent, much of it
from a collaborator on the repo (Lemire: popcount kernel, Roaring container) and some of it from
the author's own earlier tool (Tomahawk: the dense/sparse dispatch). The chemoinformatics
literature independently arrived at SIMD popcount + cache-tiled all-pairs in 2011 and at
runtime-dispatched N×N with pruning by 2019.

What remains unclaimed is the *packaging*: a general, domain-agnostic, batched all-pairs
intersection-cardinality primitive with runtime multi-ISA dispatch and runtime density adaptation.
That is an engineering contribution, and on its own it is blog-shaped, not paper-shaped.

It becomes paper-shaped if §7.1–§7.4 are done properly: a register-blocked multi-output popcount
microkernel, a vectorized mixed bitmap×list kernel, a derived and validated density-crossover
model, and density-sorted tile scheduling — benchmarked on modern AVX-512 and SVE2 hardware
against CRoaring-in-a-loop, `libpopcnt`, SimSIMD, and the published Muła/Kurz/Lemire figures.

The framing that survives review is **"batched all-pairs is a different kernel problem from
pairwise, and here is the microkernel and selection model it requires"** — not "we made popcount
faster." Venue: *Software: Practice and Experience*, *The Computer Journal*, or *ACM TOMS* —
the same journals as the prior art.
