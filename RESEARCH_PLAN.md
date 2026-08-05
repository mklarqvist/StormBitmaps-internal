# StormBitmaps — Research Plan

**Scope:** kernels and algorithms only. No application layer, no domain integration.
**Objective:** see **`PROBLEM_STATEMENT.md` — read that first.** This document is subordinate to
it; where they disagree, the problem statement wins.
**Prior-art basis:** `LANDSCAPE.md`. Note its §7 predates the pairing-matrix reframing; its
prior-art findings stand, its ordering of contributions does not.

---

## 0. Thesis

> The waste is not slow popcount — it is **fixed-cost bitmap × bitmap applied to pairs that
> deserve near-zero cost.** Select the best-matched *pair of representations* per pair and per
> chunk, then vectorize each cell of the resulting pairing matrix.

Popcount is solved (Muła/Kurz/Lemire 2018). Every existing implementation computes
`popcount(A AND B)` well and then calls it in an O(N²) loop at Θ(m) per pair regardless of
density. On realistically skewed data most pairs have at least one near-empty side, so most of
that Θ(m) is spent ANDing zeros. See `PROBLEM_STATEMENT.md` §2 for the arithmetic — the available
win is **10²–10⁵×, asymptotic**, not the ~1.7× constant factor available from better dense
kernels.

Claims **P1–P5** are defined in `PROBLEM_STATEMENT.md` §7. The research risk concentrates in:

| # | Claim | Gate |
|---|---|---|
| **P2** | Per-pair selection can be made ≤2% of runtime (O(1) metadata or tile hoisting) | **Gate 1** |
| **P3** | Vectorized asymmetric kernels beat scalar *and* inflate-to-bitmap fallback | **Gate 2** |
| **P4** | A rank index makes B×R cost Θ(runs), independent of run length | Phase 3 |
| **P1/P5** | Adaptive pairing beats all-bitmap ≥50×, and beats CRoaring on skewed data | **Gate 3** |

P1 is near-arithmetic rather than a research question. **P2 and P3 are where this succeeds or
fails**, and the phase order below reflects that.

---

## 1. Kernel taxonomy — the pairing matrix

The unit of work is a **cell** of the representation cross-product (`PROBLEM_STATEMENT.md` §3).
Representations: **B** bitmap, **S** sorted array, **R** run/RLE, **W** WAH-style fills,
**Ro** Roaring (meta — decomposes into the others per chunk).

Priority is set by `PROBLEM_STATEMENT.md` §6: the asymmetric cells carry the real-world benefit.

| Cell | Cost | Priority | Status |
|---|---|---|---|
All ten non-Roaring cells are **built, differentially tested and closed** as of 2026-08-04
(`results/OPTLOG.md`): 13 optimization rounds, ~180 variants, every cell 5+ consecutive iterations
without an improvement above the 5% noise floor, 2.4–2.7 M correctness checks per host.

| Cell | Cost | Priority | Status — winning variant |
|---|---|---|---|
| **B × S** | Θ(\|S\|) | **P0** | CLOSED — `ilp8`/`shift`, plain scalar. NEON variants exist and lose |
| **B × R** | Θ(r) with rank index | **P0** | CLOSED — `hybrid4`/`rank`. **P4 proven** |
| **S × R** | Θ(\|S\| + r) or Θ(\|S\| log r) | **P0** | CLOSED — `adapt_b6`, three-way cost selection |
| **B × W** | Θ(literals + fills) | P1 | CLOSED — `occ`/`skip`/`rank` by corpus |
| **S × W** | Θ(\|S\| + fills) | P1 | CLOSED — `adapt_f1`, gallop past fills |
| **R × R** | Θ(r_A + r_B) | P1 | CLOSED — `adapt_r6` |
| **R × W**, **W × W** | Θ(r_A + r_B) | P2 | CLOSED — `skip2`, bulk fill skipping |
| **S × S** | Θ(\|A\|+\|B\|) merge / galloping | P1 | CLOSED — `adaptive2` → NEON 8×8 block compare or `gallop_sym` |
| **B × B** | Θ(m) naive; **sub-linear zone-mapped** | P2 | CLOSED — `occ_sel`. **No AVX-512 kernel: loses 5× to CRoaring on x86 dense data** |
| **Ro × ·** | per-chunk dispatch | P1 | NOT BUILT, deliberately — CRoaring covers the 3×3 sub-case and Ro is a *meta* representation belonging in the selection layer |

**Selection machinery** — not a cell, but the load-bearing component:

| ID | Component | Status |
|---|---|---|
| **M1** | Per-row metadata (cardinality, run count, chunk occupancy) | **DONE** — `RowMeta`, `kernels/storm_repr.h` |
| **M2** | O(1) pairing decision from metadata | **DONE but feeds a failing model** — `select_pairing()`, `kernels/storm_cost.h` |
| **M3** | Tile-level hoisting via density/run-count partitioning | **NOT STARTED — and it is now the critical path.** Gate 1 fails and this is the plan's own prescribed remedy |
| **M4** | Calibrated cost model producing the decision thresholds | **DONE, FAILS GATE 1** — 94.1% regret, 28.3% selection overhead vs a 2% budget. `kernels/storm_cost.cpp`, `bench/bench_select.cpp` |
| **M5** | Rank/prefix-popcount index | **DONE** — rank9-style, `build_rank()`. Plus a **zone map** (`build_occ()`, 0.195% overhead) that the plan did not anticipate and that outperforms it for B × B |

`B × B` is deliberately **P2**. It is ~1% of pairs on skewed data and the only cell where fixed
cost is unavoidable; optimizing it first would be optimizing the slice that matters least.

---

## 2. The blocking hierarchy

Five levels. The current repo implements L0, L3, and L4 only — **L1 and L2 are missing**.

Note the scope: this hierarchy governs the **B × B cell**, which is P2 priority (~1% of pairs on
skewed data). It is a completeness item, not the headline. The asymmetric cells of §4 have their
own, quite different, cost structure — their work is proportional to cardinality or run count, so
register blocking does not apply to them in this form.

| Level | Name | Unit | Current state |
|---|---|---|---|
| **L0** | Lane | SIMD width (512b = 8 words) | Done, via `libalgebra` |
| **L1** | **Register** | MR×NR accumulators in ZMM/YMM/Z-regs | **Missing — §3** |
| **L2** | **L1-cache panel** | Packed panel of vectors resident in L1d | **Missing — §3.4** |
| **L3** | L2/L3 cache block | `STORM_CACHE_BLOCK_SIZE` 256 kB | Present (`STORM_wrapper_diag*_blocked`) |
| **L4** | Schedule | Triangle traversal, thread partition, density order | Partial; no threading; §5 |

This mirrors Goto/van de Geijn and BLIS. Their contribution was never cache blocking alone — it
was the **packed microkernel with register blocking**, plus packing to make the microkernel's
access pattern unit-stride. Storm implements the outer loops of that structure and stops before
the part that matters.

---

## 3. B × B — the register-blocked microkernel

### 3.1 The problem with the current inner loop

`STORM_wrapper_diag_blocked` (`storm.cpp`) and `STORM_wrapper_diag_list_blocked`
block for cache, but the innermost statement is still `f(left, right, n_ints)` — a **pairwise**
kernel invocation. Each call re-streams both operands end to end.

Per output vector-pair, a pairwise kernel issues:

```
vmovdqa64  zmm_a, [A]      ; load
vmovdqa64  zmm_b, [B]      ; load
vpandq     zmm_t, zmm_a, zmm_b
vpopcntq   zmm_c, zmm_t
vpaddq     zmm_acc, zmm_acc, zmm_c
```

**2 loads : 1 popcount.** To sustain 1 VPOPCNTQ/cycle you need 2 loads/cycle, which is exactly
the L1d limit on most cores and unreachable from L2. The kernel is therefore load-bound and can
never hit the popcount-port ceiling.

### 3.2 The fix

Hold MR left vectors in registers, stream NR right vectors, accumulate MR×NR separate counters:

```
for k in words:
    load B_0..B_{NR-1}                    ; NR loads
    load A_0..A_{MR-1}                    ; MR loads
    for i in MR, j in NR:
        acc[i][j] += popcount(A_i & B_j)  ; MR*NR popcounts
```

**Load:popcount ratio drops from 2:1 to (MR+NR)/(MR·NR).**

| MR×NR | Loads:popcnt | ZMM registers needed | Fits 32? |
|---|---|---|---|
| 1×1 (pairwise, today) | 2.00 | 1 + 2 | ✔ |
| 2×2 | 1.00 | 4 + 4 = 8 | ✔ |
| 4×2 | 0.75 | 8 + 6 = 14 | ✔ |
| **4×4** | **0.50** | 16 + 8 = 24 | ✔ **primary candidate** |
| 5×4 | 0.45 | 20 + 9 = 29 | ✔ tight |
| 8×2 | 0.63 | 16 + 10 = 26 | ✔ |
| 5×5 | 0.40 | 25 + 10 = 35 | ✘ spills |

Register budget = MR·NR accumulators + MR held + NR streamed. AVX-512 has 32 ZMM; AVX2 has 16 YMM
(so AVX2 tops out around 2×2 or 3×2); NEON has 32 × 128-bit; SVE2 has 32 Z-registers.

**4×4 on AVX-512 is the primary target.** Also build 2×2, 4×2, 8×2, 5×4 and measure — the optimum
is microarchitecture-dependent and this is exactly the kind of thing the community benchmark
matrix (§7) should settle rather than one machine's intuition.

### 3.3 The ceiling, derived

> ⚠ **Tier-1 (recalled) figures — not citable as written.** Everything below is recalled from
> memory, not sourced. Raise to *sourced* with the **`lookup-intel-intrinsics`** skill
> (Intel-published latency/throughput, instruction/XED mapping, CPUID gates), then to *measured*
> with `perf`/`uops` counters in Phase 5. See `AGENTS.md` → evidence ladder. Applies to the port
> assignments, the 0.125 c/word ceiling, and the Harley-Seal argument that rests on them.
>
> **The AArch64 half of this is now settled — and it inverts the conclusion.** See §3.3a.

### 3.3a AArch64 — sourced and measured, and the load-bound premise does not hold

The analysis below §3.3 is specific to AVX-512, where `VPOPCNTQ` occupies a port of its own and
the AND/ADD issue elsewhere. **On NEON every operation in the kernel competes for the same four
pipes**, so the conclusion reverses.

Sourced (tier 2, `applecpu`, Firestorm — *not* this host, so used as hypothesis only):

| Instruction | LAT | recip TP | Units |
|---|---|---|---|
| `AND` / `CNT` / `UADALP` / `ADDP` (16B) | 2–3 | **0.25** | u11-14 — 4 SIMD pipes |
| `LDR` (Q) | ≤9 | **0.333** | u8-10 — 3 load pipes |

Per 16 bytes the kernel issues **3 SIMD ops against 2 loads** → 0.75 cyc/16B from the SIMD pipes
versus 0.667 from the load pipes. The kernel is **SIMD-issue bound, not load bound**, and the
derived ceiling is **0.375 cycles/word**.

Measured (tier 3, Apple M4, `bench/bench_cells.cpp`): **0.42–0.49 cycles/word** at L2 residency,
against that 0.375 ceiling. What moved the number was accumulator count, not instruction
selection: identical instruction mix measures 0.41× with one accumulator and ~1.24× with eight,
because `UADALP` has latency 3 and four pipes retire 4/cycle.

**Consequence for §3.2.** MR×NR register blocking exists to cut *loads*. On this ISA loads are
not the binding resource, so the expected payoff for B×B is small or zero. A cheap proxy for the
full experiment — replacing paired `LDR` with `LD1 (multiple, 2 regs)`, halving load instructions
without changing the SIMD op count — is in the harness as `neon_ld2`. Phase 5 should treat "does
register blocking pay on AArch64?" as an open question with a negative prior, not as a port of the
AVX-512 result.

**Harley-Seal (the §3.3 corollary) is confirmed by measurement, on the ISA where the argument is
strongest.** A CSA is 5 ops and buys back `CNT`s that cost 1 op each at 4/cycle; reducing 16
vectors costs 15 CSAs = 75 ops to remove ~30. Measured **0.38–0.72×** across corpora. Retained in
the harness as a labelled negative control.

Per output on AVX-512: `VPANDQ` (p0/p1/p5) + `VPOPCNTQ` (**p5 only**, 1/cycle on Ice Lake, Sapphire
Rapids, Zen 4) + `VPADDQ` (p0/p1/p5).

Port 5 is the bottleneck at 1 VPOPCNTQ/cycle = 8 words/cycle = **0.125 cycles/word**.
The 2019 measurement of 0.21 is ~60% of that → **~1.7× headroom**, and the gap is precisely the
load pressure that register blocking removes.

**Corollary — kill Harley-Seal on the AVX-512 path.** CSA exists to reduce popcount *instruction
count* on ISAs with no vector popcount. With native VPOPCNTQ the trade inverts:

```
CSA(a,b,c) -> (h,l):  l = a^b^c            ; 2 XOR
                      h = (a&b) | ((a^b)&c) ; 2 AND + 1 OR   → 5 ALU ops
```
3 popcounts → 2 popcounts + 5 ALU ops. You free **1** p5 slot and add **5** ops spread over 3 ALU
ports ≈ 1.67 cycles. Net loss. Verify by measurement, but the prior is strong and the arithmetic
is in the plan so a reviewer can check it.

Keep Harley-Seal only on AVX2 and any path lacking a native vector popcount.

### 3.4 Packing (L2)

The microkernel wants unit-stride, aligned, contiguous access to its MR and NR operands. In the
diagonal/triangular traversal the natural layout does not give that. Pack a panel of vectors into
a scratch buffer sized to L1d before entering the microkernel, BLIS-style. Measure whether the
packing cost amortizes — for long vectors it should; for short (fingerprint-length) vectors it may
not, which is itself a reportable result.

### 3.5 Output bandwidth — a real constraint, and where it binds

Materializing the full N² matrix is often the actual bottleneck, and this determines which API
forms are worth optimizing.

Per pair: 4 bytes out vs `m · 0.125` cycles of compute (m = vector length in 64-bit words).
Assuming ~1 byte/cycle of sustained write bandwidth, the kernel is **output-bound when
m ≲ 32 words (2048 bits)**.

| N | m | Pairs | Output | Compute @0.125 c/w, 3 GHz | Bound by |
|---|---|---|---|---|---|
| 10,000 | 16 | 5.0e7 | 200 MB | 0.03 s | **output** |
| 100,000 | 16 | 5.0e9 | 20 GB | 3.3 s (→6 GB/s out) | **output** |
| 100,000 | 1,000 | 5.0e9 | 20 GB | 208 s (→0.1 GB/s out) | compute |
| 1,000,000 | 1,000 | 5.0e11 | 2 TB | 5.8 h | **infeasible to materialize** |

Two consequences:

1. The fingerprint-length regime (1024–2048 bit) that chemoinformatics cares about is
   **output-bound, not compute-bound**. That is the real reason chemfp uses thresholds and top-k
   rather than full matrices — worth stating explicitly, since it reframes what "faster kernel"
   even means there.
2. The API must offer forms that never materialize N². See §6.

---

## 4. The asymmetric cells — B × S, B × R, S × R

**This is the highest-value work in the project** (`PROBLEM_STATEMENT.md` §2.3, §6): the mixed
cells are ~18% of pairs on skewed data, and handling them badly — by inflating the sparse side to
a bitmap and falling back to B × B — recovers **zero** of the available saving on nearly a fifth
of all work.

The seam between two literatures: SIMD set-intersection papers cover list×list; popcount papers
cover bitmap×bitmap. **Nobody vectorized the mixed case.** Storm's version is scalar *and* carries
the precedence bug, so the load-bearing path is both unwritten and unmeasured.

Always iterate the **shorter** representation against the denser one.

Four candidate designs, to be built and raced against each other and against scalar:

> **Measured status (Apple M4, `results/OPTLOG.md`).** Two of the four designs below **do not
> exist on AArch64**: NEON has neither a gather (D1) nor `VPCONFLICTD` (D3). The portable field is
> D2 and D4 plus ILP restructuring.
>
> **D2 — run collapsing — is the design this section bets on, and it loses.** Measured
> **0.25–0.51×** against the straight-line kernel, *including on clustered data*. The mechanism:
> folding a same-word group costs one unpredictable branch per group, and at a mean run of ~10
> bits that is one misprediction per ~10 elements ≈ 2 cycles/element — which is what the
> straight-line version costs anyway. It trades ILP for fewer loads, and on this core loads are
> cheap while mispredictions are not. **D4 (prefetch) also loses** (0.47–0.65×): the list is
> sorted, so the access stream is monotonic and the hardware prefetcher already has it.
>
> What wins is prosaic: branchless `(w>>bit)&1` and eight *named* accumulator chains, at ~1.14
> cycles per list element against a load-port floor of 0.67.
>
> **The finding that matters more than any of this**: on clustered data the right answer is not a
> better B×S kernel, it is to stop using B×S. On corpus C1, B×R costs 18.1 ns/pair against B×S's
> 42.3 for the same rows. That is the pairing matrix working as designed, and it means B×S's real
> job is the *unclustered* sparse case — precisely where D2 has nothing to exploit.

**D1 — Gather.** Load 16 × `uint32` positions; word indices `v >> 6`; `VPGATHERDD`/`VPGATHERQQ`
from the bitmap; bit masks via `VPSLLVQ` of `1 << (v & 63)`; `VPTESTMQ` → mask; `KPOPCNT` the
mask register. Gathers are slow (multi-cycle throughput) but amortize over 8–16 lookups. Likely
best at moderate list length.

**D2 — Run collapsing.** A *sorted* list has many consecutive values sharing a 64-bit word.
Detect maximal runs where `v[i] >> 6 == v[i+k] >> 6`, build one combined mask for the run, do a
single load + AND + popcount. Best when the list is locally clustered — which real data is and
uniform-random synthetic data is not. This is the design most likely to win on realistic input
and most likely to be missed by a synthetic benchmark. See §7.2.

**D3 — Conflict detection.** `VPCONFLICTD` over the word-index vector to identify lanes targeting
the same word, merge their masks, then one load per distinct word. A branch-free generalization of
D2.

**D4 — Software-pipelined scalar.** For very sparse lists, plain scalar with prefetch may beat all
vector approaches. Note `STORM_intersect_bitmaps_scalar_list` already contain commented-out
`__builtin_prefetch` calls — someone started this and stopped. Finish it; it is the honest
baseline the vector kernels must beat.

Deliverable: a density-parameterized comparison of D1–D4 plus scalar, per ISA. Even a negative
result ("gather never wins; run-collapsing wins above clustering coefficient X") is publishable
content and is genuinely unknown today.

Every design must also be raced against the **inflate-to-bitmap fallback** (materialize the sparse
side, run B × B). That is what a naive implementation does, and beating it is claim P3.

### 4.5 B × R and the rank index (M5)

The strongest single idea in the project — see `PROBLEM_STATEMENT.md` §4.

For cardinality only, a run `[a, b)` on one side contributes exactly `popcount(other[a..b))`. With
a prefix-popcount (rank) index over the dense side that is **O(1) per run**:

```
contribution = rank_B(b) − rank_B(a)
```

So **B × R costs Θ(r) — the number of runs — independent of run length.** A row that is one
9-million-bit zero run plus one 1-million-bit one run costs two operations, not 156,250. This is
the "99% empty ⇒ 99% of work eliminated" mechanism made O(1) rather than O(length).

Rank/select is mature (Jacobson; Clark; Vigna's `rank9`; `sdsl`) with ~25% space overhead for
constant-time rank, but the prior-art search found **no application of it to all-pairs
intersection cardinality against run containers**. Cheap to test, and a real contribution if it
holds.

> **P4 — MEASURED, and it holds.** `bench/p4_runlength.sh`, Apple M4, 1,048,576-bit universe, run
> **count** pinned at ~15.3 per pair while run **length** varies 256×:
>
> | mean run | no-index (NEON) | rank9 index | ratio |
> |---:|---:|---:|---:|
> | 64 b | 1.72 ns/run | 2.20 ns/run | 0.78× |
> | 256 b | 2.91 | 2.51 | 1.16× |
> | 1024 b | 4.80 | 2.74 | 1.76× |
> | 4096 b | 10.46 | 4.49 | 2.33× |
> | 16384 b | 31.75 | 3.29 | **9.65×** |
>
> The no-index kernel grows 18× across the sweep; the indexed one shows no trend. **Run length
> drops out of the cost**, which is exactly and only what P4 asserts. Reporting the two axes
> separately, as this section demands, is what makes that visible — a single "runs/second" figure
> would have hidden it.
>
> **Amortization threshold, measured: ~256-bit runs (4 words).** The analytic estimate was
> 600–1000 bits and was wrong in the expensive direction — it counted instructions, when the
> no-index path's real cost past L1 is memory traffic. Shipping threshold corrected from 12 words
> to 4. A worked example of why `AGENTS.md` rule 8 exists.
>
> **The same index does accelerate B×W** (the last bullet below): 1.20× on long-fill data, since
> a one-fill is a run. Bounded by the literal fraction of the stream, which is irreducible.

Experiments:
- `rank9`-style two-level index vs block-granularity rank (coarse counters + local popcount);
  space/time trade-off curve
- B × R timing as a function of run count **and** run length, reported **separately** — the claim
  is specifically that length drops out (P4)
- When is building the index worth it? Amortization threshold in number of run-pairings a row
  participates in
- Does the same index accelerate B × W (fills are runs)? Expected yes; confirm
- Interaction with §3.5 output bandwidth: rank-index B × R is so cheap per pair that output
  writing dominates immediately

---

## 5. Selection model and scheduling

### 5.1 Replace the magic numbers

Currently hardcoded guesses:
- `STORM_contig_new()` — `scalar_cutoff = min(vector_length / 200, 200)`
- `storm.h:46` — `STORM_DEFAULT_SCALAR_THRESHOLD 4096`

Replace with a cost model over three calibrated constants:

```
C_bb = m · c_bb                      ; dense × dense — independent of density
C_bl = |L_min| · c_bl                ; dense × list
C_ll = f(|L_1|, |L_2|) · c_ll        ; list × list (merge or galloping)
```

`c_bb`, `c_bl`, `c_ll` are measured once by a startup (or build-time) microbenchmark, per ISA and
per cache level of residency. Thresholds are then *derived*: bitmap×bitmap wins over bitmap×list
when `|L| / m > c_bb / c_bl`.

This is the piece that generalizes. EmptyHeaded did it for relational joins; RISC-vs-chemfp
established the crossover exists in chemoinformatics but treats it as a design-time choice between
separate tools. A calibrated, portable, runtime model for this kernel does not exist.

**Validation:** predicted vs actual optimal kernel across a dense grid of (density, m, N, ISA).
Report the model's regret — how much slower than an oracle that always picks correctly. Regret,
not raw speed, is the metric that makes this a contribution.

### 5.2 Density-sorted tile scheduling

The kernel choice is currently per-pair → a branch inside the hot loop. Sort or partition vectors
by popcount so that dense-dense, dense-sparse, and sparse-sparse pairs form **contiguous
homogeneous tiles**, each running one specialized branch-free kernel.

Note: chemfp popcount-sorts too, but for memory coherency and BitBound pruning — not to partition
the schedule by kernel. The mechanism is shared; the purpose is new.

Costs to account for honestly: the sort itself, and the permutation bookkeeping (emit
`(orig_i, orig_j, count)` triples or maintain an inverse permutation). Report both.

### 5.3 Triangle traversal and threading

The repo is single-threaded. Upper-triangle work is non-uniform, so naive row partitioning gives
poor load balance. Evaluate: block-cyclic assignment, Morton/Z-order (Haque et al. used this for
locality; reuse for balance), and the affine-plane decomposition of Sapin & Keller.

Threading is deliberately *late* in the plan. Single-thread cycles/word is the honest kernel
metric and the one comparable to the published literature; parallel speedup can hide a bad kernel.

---

## 6. API

The batched primitive is the deliverable. Four output forms, because §3.5 shows materialization is
frequently the binding constraint:

```c
/* 1. Reduction — no N² materialization. The regime where kernel speed purely dominates. */
uint64_t STORM_allpairs_sum(const uint64_t* data, size_t n_vec, size_t n_words);

/* 2. Tile visitor — caller consumes each tile; enables streaming and fusion. */
typedef void (*STORM_tile_cb)(size_t i0, size_t j0, size_t mr, size_t nr,
                              const uint32_t* counts, void* ctx);
int STORM_allpairs_tiles(const uint64_t* data, size_t n_vec, size_t n_words,
                         STORM_tile_cb cb, void* ctx);

/* 3. Thresholded — emit only pairs with |A∩B| >= t. Where a pruning bound would attach. */
size_t STORM_allpairs_threshold(const uint64_t* data, size_t n_vec, size_t n_words,
                                uint32_t t, STORM_pair* out, size_t out_cap);

/* 4. Full materialization — only sane for small N; keep for reference and testing. */
int STORM_allpairs_matrix(const uint64_t* data, size_t n_vec, size_t n_words, uint32_t* out);
```

Plus a rectangular `STORM_xy_*` family (two distinct input sets) — the triangle case is the
special case, not the other way round.

**Explicit non-goal:** Swamidass–Baldi popcount-bound pruning is a real gap (`LANDSCAPE.md` §3.2)
but it is an *algorithmic* addition that changes the complexity class, not a kernel. Form 3 leaves
the hook. Decide after Gate 2 whether it is in scope; do not let it displace K2/K4.

---

## 7. Benchmarking methodology

The place kernel papers most often fail review. Fix it up front.

### 7.1 Metrics — report all four, always

1. **Cycles per 64-bit word** (single-thread) — comparable to Muła/Kurz/Lemire Table 4.
2. **Wall clock** — the number that is not gameable.
3. **Effective bandwidth** — with cache residency stated explicitly.
4. **Port/uop counters** — `perf stat -e uops_dispatched_port.*`, plus L1/L2/L3 misses. Required
   to *prove* the port-bound claim rather than assert it.

**Report cache-resident and DRAM-resident regimes separately and label them.** The existing README
quotes 114 GB/s — above that machine's DRAM bandwidth — with the qualifier `N < 256,000` buried in
prose. That framing will be attacked in review and deserves to be. Fix it in the README as part of
Phase 0.

### 7.2 Data generation — the thing most benchmarks get wrong

Uniform-random bit positions are unrepresentative: real bitmaps are **clustered**, and clustering
is exactly what D2/D3 (§4) exploit and what a uniform benchmark makes invisible.

Worse: a **uniform-density** corpus — where every row has roughly the same cardinality — makes the
entire contribution of this project invisible, because it never generates the skew that motivates
adaptive pairing. `PROBLEM_STATEMENT.md` §6 flags this as the single easiest way to accidentally
benchmark the result away.

Two independent axes must both be swept:

**(i) Within-row structure** — how set bits are distributed inside one row:
- Uniform random (worst case for clustering) — one point in the study, not the whole study
- Markov/gap model with tunable autocorrelation → controllable clustering coefficient
- Zipfian gap distribution
- Run-length-structured (blocks of set bits) — the regime where R and W representations win

**(ii) Across-row cardinality distribution** — how densities are distributed over the corpus:
- **1/i frequency spectrum** (`PROBLEM_STATEMENT.md` §2.2) — **mandatory**; the neutral-coalescent
  shape, singleton-dominated. This is the headline benchmark
- Uniform density (all rows alike) — the misleading classic; include it *labelled as such*
- Bimodal (a dense minority + a sparse majority) — stresses the mixed cell specifically
- Empirical spectrum from a real cohort's allele-frequency distribution, if obtainable

At minimum one **real** dataset dumped to raw bitmaps as a sanity anchor.

Parameter sweep: density ∈ [1e-7, 0.5] log-spaced (note the extended low end — singletons in a
10⁷-bit universe sit at 1e-7); m ∈ {16, 64, 256, 1024, 16384, 156250} words;
N ∈ {1e3, 1e4, 1e5}; clustering ∈ {none, moderate, heavy};
spectrum ∈ {uniform, 1/i, bimodal, empirical}.

**Always report the 1/i-spectrum result alongside the uniform one.** The gap between them *is* the
contribution.

### 7.3 Baselines — implement fairly or the result is worthless

| Baseline | Represents |
|---|---|
| Scalar `__builtin_popcountll` loop | Floor |
| `libpopcnt` + manual AND | Best-practice pairwise, runtime-dispatched |
| CRoaring `roaring_bitmap_and_cardinality` in an O(N²) loop | The thing people actually do today |
| SimSIMD Jaccard kernels | Current hand-tuned pairwise state of the art |
| Muła/Kurz/Lemire published Table 4 figures | The literature's number, normalized |
| Storm 2019 (post-bugfix) | Our own starting point |

Tune each baseline in good faith — same compiler flags, same alignment, same warmup. A rigged
baseline is the fastest way to lose a reviewer.

### 7.4 Correctness

- Reference implementation: naive scalar, obviously correct, used as oracle.
- Every kernel × every ISA × every blocking factor differential-tested against the oracle.
- **Fuzzing** (libFuzzer/AFL) over `{n_vec, n_words, density, clustering, alignment}`.
- **Intel SDE** for ISAs not physically available — correctness without hardware.
- Edge cases the current code gets wrong: duplicate input values, empty vectors, `n_words` not a
  multiple of the SIMD width, unaligned buffers, `n_vec` < blocking factor.

---

## 8. Phases and gates

Ordered by where the value is (`PROBLEM_STATEMENT.md` §6), not by what is easiest to measure.

| Phase | Work | Est. | Exit |
|---|---|---|---|
| Phase | Work | Status (2026-08-04) |
|---|---|---|
| **0** | Foundation, representation layer, skewed generators | **DONE** |
| **1** | Selection machinery M1–M3 → **GATE 1 (P2)** | **DONE — GATE PASSED** at 0.10–0.48% via tile hoisting + probe |
| **2** | Asymmetric cells → **GATE 2 (P3)** | **DONE**, answered *negatively*: SIMD wins 0 of 25 points. The plan's "scalar wins everywhere" branch |
| **3** | Rank index, B×R/B×W (M5) | **DONE — P4 holds**, crossover measured at ~256-bit runs |
| **4** | Cost model (M4) + density-sorted tiling → **GATE 3 (P1/P5)** | **P5 PASSED** (1.7–19.1× over tuned CRoaring, 4 hosts). **P1 conditional** (55–91× on run data; not mid-band). Regret unmeasured for the probe policy |
| **5** | B×B dense cell | **DONE** — register blocking refuted; AVX-512 kernel added, 10× |
| **6** | ~~Threading + triangle load balance~~ | **MOVED TO TOMAHAWK.** This repo is algorithms and kernels only; threading, I/O scheduling and load balancing are a layer above it |
| **7** | Cross-ISA + community | **4 ISAs measured** (NEON, SVE, SVE2, AVX-512). Community mechanism (§9.1) not built |
| **8** | Write-up | **NOT STARTED** — but the evidence base is now sufficient |

### Phase 0 — Foundation — **substantially COMPLETE (2026-08-04)**

Done:

- ✅ `libalgebra` submodule initialised; CRoaring vendored and pinned at **v4.7.2**.
- ✅ All 13 defects in `LANDSCAPE.md` §8 fixed, including the precedence bug that corrupted every
  bitmap↔scalar path (the entire mixed-density mode) and the UB in the B×S kernel.
- ✅ **Test suite built** — `tests/test_storm.c`, 1,279 checks against an independent oracle,
  wired into CTest. Every fix verified by reverting it and confirming failure.
- ✅ Repo builds on arm64 (it previously did not compile at all) and configures on CMake ≥ 4.
- ✅ Switched to **C++17 internals with an `extern "C"` public ABI**; `tests/test_storm.c` stays C
  so it doubles as the ABI regression test. Verified: 42 unmangled `STORM_` exports, 0 mangled.

- ✅ **Representation layer** — `kernels/storm_repr.{h,cpp}`. B, S, R (SoA `[start, end)`),
  W (EWAH-64), rank9 index, and per-row metadata M1. Ro is deliberately not built: CRoaring
  already covers its 3×3 sub-case and it is a *meta* representation, so it belongs in the
  selection layer rather than as a 5th set of kernels.
- ✅ **Skewed data generators** (§7.2) — `kernels/storm_gen.{h,cpp}`. Both axes independent:
  within-row structure (uniform / Markov-gap clustered / run-structured) × across-row spectrum
  (uniform / **1-over-i** / bimodal). The 1/i upper limit is *solved* so its mean cardinality
  matches the uniform spectrum's — otherwise the two corpora differ in density as well as skew and
  the comparison measures the wrong thing, flattering the result for the wrong reason.
- ✅ **All ten non-Roaring cells implemented and differentially tested** — 1.93 M checks against
  an independent oracle (`tests/test_cells.cpp`, wired into CTest), sweeping universes that are
  not multiples of 64/512/the rank stride, empty/full/single-bit/single-run rows, and both
  argument orders.
- ✅ **Per-cell benchmark harness** — `bench/bench_cells.cpp` + `bench/sweep.sh` +
  `bench/compare.py`. Reports ns/pair, ns per work unit, derived cycles per work unit, corpus
  footprint against this host's cache hierarchy, and a measured clock. Emits JSON (§9).
- ✅ **libalgebra vendored** with the arm64 portability fix — a fresh clone now builds on arm64.

Outstanding:

- ⬜ JSON results schema + `plot.py` (§9.1) — the harness emits records, but there is no
  versioned schema and no figure regeneration yet.
- ⬜ Fix the README's 114 GB/s framing (§7.1). The Status block flags it as unverified; the
  table itself still stands unqualified.

**Results so far: `results/OPTLOG.md`** — per-cell iteration log, hypotheses and refutations
included.

Phase 0 additionally must deliver the **representation layer** (B/S/R/W/Ro constructors,
converters, and per-row metadata M1) and the **skewed data generators** of §7.2 — a 1/i frequency
spectrum and linkage-like clustering. Without those generators every later measurement is
meaningless, because uniform-density data makes this entire contribution invisible.

### Phase 1 — Selection machinery → **GATE 1 (P2) — PASSED 2026-08-04**

Per-pair selection measured **28–48% of runtime** across three hosts — a clear
fail. §8's own remedy (tile hoisting) applied, plus one thing this plan did not
anticipate:

| host | per-pair | **per-tile** | **probe-and-commit** |
|---|---:|---:|---:|
| apple-m4 | 34.9% FAIL | 0.19% PASS | 0.30% PASS |
| neoverse-sve2 | 27.7% FAIL | 0.10% PASS | 0.48% PASS |
| sapphire | 48.4% FAIL | 0.29% PASS | 0.31% PASS |

**P2 holds in its tile-hoisted form.** Decisions drop 130,816 → 36.

The unanticipated part: hoisting fixed the *cost* of selection and exposed a
*quality* problem it had been masking — the model's tile choice measured 0.66×
on neoverse-sve2, worse than no selection at all. The fix is **probe-and-commit**
(`kernels/storm_allpairs.cpp`): time the candidates on three of a tile's ~4,000
pairs and commit the winner, ~0.2% of the tile. B×B is always a candidate, which
bounds the downside. That recovers 0.66× → 1.09× and improves the other hosts.

**The consequence for §5.1 is significant: the calibrated cost model does not
have to be right.** It only has to nominate a candidate worth timing, because
the timing amortizes over thousands of pairs. That substantially weakens this
project's dependence on per-ISA constant fitting, which §5.1 treats as the hard
part.

### Phase 1 — original text

Build M1–M3 and a correct-but-unoptimized version of every pairing cell (scalar reference
implementations are fine here). Measure what fraction of runtime the *decision* consumes at N²
scale, with and without tile hoisting.

- **Selection ≤2% of runtime** → P2 holds, proceed.
- **2–10%** → proceed, but M3 tile hoisting becomes mandatory rather than optional.
- **>10% and not reducible** → the per-pair adaptive premise is in trouble. Fall back to
  per-tile-only selection and re-scope. This is the cheapest possible test of the core premise —
  run it before writing a single line of SIMD.

### Phase 2 — Asymmetric cells → **GATE 2 (P3)**

Fix the broken B × S scalar path first — it is the honest baseline. Then build D1–D4 (§4) and race
them across the density × clustering grid, per ISA, against both scalar and the
inflate-to-bitmap fallback.

- **Vectorized asymmetric kernels win over a measurable density band** → P3 holds; this is the
  paper's core kernel result.
- **Scalar wins everywhere** → still a result (and a useful one), but the kernel contribution
  shrinks to selection + rank index. Reassess scope.

### Phase 3 — Rank index

M5 and the B × R / B × W cells (§4.5). Test P4: run length must drop out of the cost.

### Phase 4 — Cost model and tiling → **GATE 3 (P1/P5)**

M4 calibration microbenchmark, derived thresholds replacing `STORM_contig_new()` and `storm.h:46`,
oracle-regret measurement across the full grid, density-sorted tiling.

- **Regret <5%, ≥50× over all-bitmap on skewed data, beats CRoaring** → P1/P5 hold. Paper is
  viable; write it.
- **Regret <5% but constants need per-ISA refitting** → fine; that *is* the calibration story.
- **Regret high / model unstable** → library + blog post. Do not force it.

### Phase 5 — B × B register blocking

The dense cell only (§3). Implement 2×2, 4×2, 4×4, 5×4, 8×2 on AVX-512; measure cycles/word and
port counters; confirm or refute the ~0.125 c/word ceiling. Worth ~1.7× on ~1% of pairs, so it is
a completeness item, not a headline. A negative result here ("the all-pairs popcount kernel is not
load-bound the way GEMM is") is independently interesting and costs three days.

### Phase 6 — Threading and load balance

Triangle partitioning, thread scaling. Kept late so single-thread numbers stay honest and
comparable to the published literature.

### Phase 7 — Cross-ISA and community (rolling, starts at Phase 2)

AVX-512 (Ice Lake, Sapphire Rapids, Zen 4, Zen 5), AVX2, NEON (Apple M-series), SVE2 (Neoverse
V2 / Graviton 3–4). **Most of this hardware will come from contributors, not from us.** See §9 —
this is the mechanism, and it is what makes P1/P5 credible.

### Phase 8 — Write-up

Target: *Software: Practice and Experience*, *The Computer Journal*, or *ACM TOMS*. Same journals
as the prior art. Blog post ships regardless of the paper decision.

---

## 8b. Programme sequence — Storm, then Tomahawk

Two papers, strictly ordered. **Storm first; Tomahawk does not start until Gate 3 clears.**

| | **Project 1 — StormBitmaps** | **Project 2 — Tomahawk** |
|---|---|---|
| Type | Kernel / algorithms paper | Application paper |
| Claim | Pairing matrix + selection model + asymmetric kernels | Genome-wide LD at a scale/cost others cannot reach |
| Venue | SPE / Computer Journal / TOMS | Bioinformatics / GigaScience / Genome Research |
| Data | Synthetic, haplotype-*shaped* (§7.2) | Real cohorts; head-to-head vs PLINK2 `--r2`, emeraLD |
| Baselines | CRoaring, libpopcnt, SimSIMD, Muła Table 4 | PLINK2, emeraLD, LDstore |
| Status | This plan | `LANDSCAPE.md` §3.1, §4.3 — repo dormant since 2019-11-09 |

**Why this order is the right one.** `LANDSCAPE.md` §4.3 found that a Tomahawk paper *today* would
fail: it has no published paper, its documented Algorithm 2/3 are the same dense/sparse dispatch
Storm inherits, and it has never been benchmarked head-to-head against PLINK2 or emeraLD on real
data. An application paper needs a demonstrated advance over best-in-class on real cohorts — and
the thing that would produce that advance is exactly the pairing matrix. Building Storm first
gives Tomahawk something real to be an application *of*.

It also resolves the self-overlap problem. Right now Tomahawk is prior art for Storm
(`LANDSCAPE.md` §3.1) — an awkward position. Publishing the kernel work first inverts the
dependency into a clean citation chain: Tomahawk cites Storm as its engine, rather than Storm
quietly re-deriving Tomahawk's unpublished internals.

**Carry-over from Storm to Tomahawk, decided in advance:**
- Storm stays domain-agnostic. No genotype encodings, no `.pgen`/VCF awareness, no r²/D′ — those
  live in Tomahawk.
- The haplotype frequency spectrum (§7.2) is a *benchmark generator shape* in Storm, nothing more.
- Pruning bounds (§6 non-goal) and LD-specific early termination are Tomahawk-side concerns.
- The four API output forms (§6) are the interface contract between the two projects — Tomahawk
  should need only the tile-visitor and thresholded forms.

**Gate:** do not open Tomahawk until Storm clears Gate 3 and the library is stable. The 2019
failure mode was exactly this — four repos started in one year, all dormant within nine months.

---

## 9. Opening it to the community

The cross-ISA validation that makes P1/P5 credible requires hardware we do not own. That is not an
obstacle to route around — it is the reason to build the project as a community benchmark from
day one.

### 9.1 The results-submission mechanism (build this in Phase 0)

```
results/
  schema.json                  # versioned result schema
  <cpu>-<microarch>-<date>.json
  plot.py                      # regenerates every figure from results/
```

`./bench --submit` runs the full sweep, emits one JSON, and prints a PR command. Contributors get
their machine into the paper's figures. We get Zen 5 and Graviton 4 data without buying either.

Every figure in the paper and the blog post regenerates from `results/` by script. No hand-curated
numbers anywhere.

### 9.2 Contribution surface

Each of these is a well-scoped, independently mergeable unit — good issue material:

- One ISA port of a pairing cell (SVE2, NEON, AVX2, RVV) — self-contained, oracle-tested
- One design of an asymmetric cell (D1–D4, §4) — self-contained, raced against siblings
- A blocking factor sweep on a microarchitecture we lack
- A data generator with a new clustering model
- A baseline wrapper (SimSIMD, CRoaring, chemfp if obtainable)

### 9.3 Project hygiene

- C99, header + implementation, Apache-2.0 (already correct)
- No mandatory dependencies beyond `libalgebra`; baselines optional at build time
- Every kernel differential-tested against the scalar oracle before merge; Intel SDE covers
  ISAs without hardware
- `CONTRIBUTING.md` stating the benchmark protocol, so submitted numbers are comparable
- Raw data and scripts public — reproducibility is the whole point and it is cheap here

### 9.4 Credit

Contributors who supply ISA ports or hardware results are acknowledged in the paper; substantial
kernel contributions warrant authorship. State this in `CONTRIBUTING.md` **before** asking for
work, not after.

---

## 10. Risks

| Risk | Response |
|---|---|
| **Selection cost eats the saving (P2 fails)** | Gate 1, Phase 1, before any SIMD is written — cheapest possible test of the core premise. Fall back to tile-only selection |
| **Asymmetric cells don't vectorize (P3 fails)** | Gate 2. Contribution shrinks to selection + rank index; still a paper, smaller |
| **Benchmarked on uniform data, result vanishes** | §7.2 makes the 1/i spectrum mandatory and requires both spectra reported side by side |
| **Reviewer: "this is Roaring"** | `PROBLEM_STATEMENT.md` §5 — four specific differences. Verify claim 3 (CRoaring mixed-container ops are scalar) against current source before asserting it |
| **Cell combinatorics explode (15 cells × 4 ISAs)** | Priority tiers in §1. P0 cells only through Gate 3; P2 cells may never be written and that is fine |
| Register blocking doesn't pay (P2 cell) | Phase 5 costs 3 days and yields a publishable negative result |
| Output bandwidth dominates the interesting regime | Already characterized (§3.5); reduction/threshold API forms are the answer, and the finding is itself reportable |
| No AVX-512/SVE2 hardware access | §9 community submissions; Intel SDE for correctness; cloud instances for spot measurements |
| Baselines tuned unfairly, undermining the result | §7.3 protocol; publish baseline configs; invite maintainers to object |
| Reviewer: "this is Muła/Kurz/Lemire + BLIS blocking" | Correct for B × B and L3 — which is why the claim is the pairing matrix, the asymmetric cells, and the selection model. `LANDSCAPE.md` concedes (a)–(e) explicitly and up front |
| Reviewer: "why not prune (Swamidass–Baldi)?" | Answer honestly: full-matrix computation is the target; pruning changes the complexity class and is orthogonal. §6 form 3 leaves the hook |
| Scope creep into applications | Out of scope by decision. Kernels and speed only |
| Solo-effort stall (the 2019 failure mode) | Gates force early kill decisions; community mechanism distributes the ISA work; blog post ships regardless |

---

## 11. Definition of done

**Minimum (library):** every P0 cell correct and tested; selection machinery M1–M3 working;
asymmetric cells vectorized on ≥2 ISAs; batched API shipped; benchmark harness public with both
uniform and 1/i spectra; results reproducible by script; README honest about cache-residency.

**Target (paper):** P1–P3 established and P4 measured; the pairing matrix beats every baseline in
§7.3 on skewed data by ≥50× and beats CRoaring head-to-head; selection overhead and cost-model
regret both quantified; ≥4 ISAs including one ARM; every figure regenerable from `results/`;
submitted.

**Then, and only then:** Tomahawk (§8b).

**Either way:** the blog post ships. It costs a week on top of work already done and it is the
outcome that survives a failed gate.


---

## 12. Literature and landscape review, 2026-08-04

Five-agent survey of papers, GitHub, adjacent techniques, application domains, and
an adversarial audit of our own claims. Findings that change the plan.

### 12.1 Three papers that partially anticipate our mechanisms — cite, do not claim around

- **Han, Zou & Yu, "Speeding Up Set Intersections in Graph Algorithms using SIMD
  Instructions," SIGMOD 2018.** Partitions each adjacency list into fixed-size
  blocks, each indexed by a bit-vector over its value range. **This partially
  anticipates the zone map** (§2.6 of the problem statement) from the graph side.
  Diff our bin granularity against their block size before claiming anything
  about zone-mapped bitmap intersection.
- **GraphTwin, PACMMOD/SIGMOD 2025.** Per-vertex 64-bit membership vectors resolve
  ~95% of queries in one cache-resident AND, falling back to adjacency lists for
  the residual. The closest published analogue to our cheap-dense-probe +
  exact-sparse-fallback structure. Different axis (which vertices get a bit slot,
  via NP-hard set packing) — but a reviewer will find it.
- **Han et al., "Accelerating Set Intersections by Reducing-Merging," KDD 2021 →
  VLDB Journal 2024.** A general framework: cheap filters shrink both operands
  before the merge, amortized across intersections sharing a list. Check its
  selection logic is not a superset of our metadata-driven selection.

### 12.2 Probe-and-commit is Micro Adaptivity — cite it by name

**Răducanu, Boncz & Żukowski, "Micro Adaptivity in Vectorwise," SIGMOD 2013.**
Keeps N alternative implementations per primitive and selects among them with an
**ε-greedy multi-armed bandit**, re-sampling continuously at vector-block
granularity (~1000 tuples) — the same altitude as our tile of ~4,000 pairs.

Our probe-and-commit is the ε→0 special case: sample once at tile entry, then
freeze. **This must be cited by name**; reviewers who know OLAP internals will
ask. It also suggests a concrete improvement: if per-pair cost drifts *within* a
tile, an ε-greedy re-probe every K tiles catches regime shifts that probe-then-
freeze misses. Lineage to cite beneath it: Kabra & DeWitt (SIGMOD 1998),
Avnur & Hellerstein Eddies (SIGMOD 2000).

### 12.3 §5.3's load-balancing citation was wrong

The plan cites "the affine-plane decomposition of Sapin & Keller." **No such paper
could be verified.** The correct citation is **Hall, Kelly & Tian, "Optimal Data
Distribution for Big-Data All-to-All Comparison using Finite Projective and
Affine Planes," arXiv:2308.15000 (2023)**, with bounds in Hall, Horsley & Stinson
(2024). It balances *pair count* per worker, not row count — which is exactly the
right unit given our 10–70× per-pair cost spread. Pair with **COWS
(TACO 2024)** / Prabhu et al. (ICS 2020) for the work-stealing layer: chunk
triangular loops by element count, never by row count.

### 12.4 SuiteSparse:GraphBLAS is the closest existing system — confirmed, not assumed

`GrB_Matrix` carries four runtime formats (sparse, hypersparse, bitmap, full) and
masked `GrB_mxm` **does** pick dot-product vs saxpy per output tile, and
Gustavson vs hash accumulation per task, from mask and operand density. That is
genuine adaptive per-operand format selection for boolean matrix products.

What it does not have: RLE/WAH representations, a zone-map pre-filter, or
selection at per-(i,j)-pair granularity. Our claim must be stated against
GraphBLAS explicitly rather than against naive all-bitmap.

### 12.5 Corrections to LANDSCAPE.md §5

- **SimSIMD was renamed NumKong** (March 2026, 1,864 stars) and is the live
  descendant, not the small `JaccardIndex` repo the landscape tracks. It ships
  emulated `VP2INTERSECT` (Diez-Cañas, arXiv:2112.06342) and SVE2 `HISTCNT`/
  `MATCH` intersection — but pairwise sorted-list only, no bitmap side, no batch.
- **VP2INTERSECT is Intel-dead / AMD-alive**: removed after Tiger Lake (never on
  Sapphire Rapids), microcoded at ~25 cycles; **AMD Zen5 added it natively**.
  Emulation beats the Intel instruction. Nobody has built a *batched* one.
- **Faiss `IndexBinaryFlat` is confirmed dumb** — plain XOR+popcount in
  `faiss/utils/hamming.cpp`, no sparsity handling at all.
- **UNSW-database/simd_set_operations** is the most complete S×S kernel survey in
  existence. Cross-check our closed S×S winners against their BMiss/galloping
  variants before claiming anything there.
- **Lemire et al., arXiv:2412.16370 (Dec 2024)** — newer AVX2/AVX-512/ASIMD
  popcount kernels. Source B×B baselines from this rather than Muła 2018.

### 12.6 The real dataset — §7.2's outstanding requirement

**1000 Genomes Phase 3, chromosome 22** is the recommendation: ~1.1–1.3M variants
× 5,008 haplotypes, public domain, direct FTP, no registration. It is the only
candidate that tests the *exact* data shape the project targets, and its skew is
theoretically grounded rather than merely observed — **Fu, "Statistical properties
of segregating sites," Theor. Pop. Biol. 48(2), 1995** derives E[ξᵢ] ∝ θ/i, which
is the canonical citation §2.2 has been missing.

Secondary cross-domain anchor, near-zero cost: **FIMI `kosarak`** (990,001 × 41,270
clickstream, direct `.gz`). Skew confirmed in market-basket, inverted-index,
recommendation and graph-degree domains — so the premise is not genomics-specific.

### 12.7 Audit findings against our own claims

- **`allpairs_tiles` has zero callers.** It compiles, is listed as "shipped", and
  is neither tested nor benchmarked. Either exercise it or stop claiming it.
- **The zone map has only ever been measured at one bin width** (512 bits,
  `OCC_BIN_WORDS` is a hardcoded `constexpr`). Every "0.195%" and "25×" figure
  describes one unswept parameterization.
- **Gate 1 is measured on 3 hosts, not 4** — Neoverse V1 is absent from every
  Gate-1 table.
- **The probe policy has no regret number.** The 94.1% figure belongs to the
  per-pair policy that failed; the policy that passes is unmeasured on the metric
  §5.1 calls "the metric that makes this a contribution".
- The AVX-512 kernel was measured at 7 density points, not on the 20-point sweep
  that produced `results/density.png` (which remains M4-only).

### 12.8 Revised priority

1. **Regret for the probe policy** — §5.1's own headline metric, currently absent
   for the only policy that passes Gate 1.
2. **1000 Genomes chr22** as the real-data anchor — closes §7.2's last hole and
   is the single most credibility-bearing missing artifact.
3. **Bin-width sweep for the zone map** — one unswept constant underpins the
   project's strongest mechanism.
4. **Exercise or withdraw `allpairs_tiles`.**
5. Threading, using Hall/Kelly/Tian for static balance + COWS for stealing.
6. Cross-pair blocking (BLIS packed-panel structure) — F11 says the DRAM win is
   here.


---

## 13. Swarm survey, 2026-08-04 — next optimization targets by regime

Four-agent survey of literature, repos and practitioner sources, run after the
1KGP3 small-universe campaign closed. Organized by regime because the answers
differ completely at the two ends.

**Scope note:** threading, work-stealing and I/O scheduling are **out of scope
for this repo** — they belong to Tomahawk. Findings below that concern them are
recorded for that project and explicitly excluded here.

### 13.1 SMALL regime (632 B/row, L1-resident) — the plateau is a PORT limit

The 1KGP3 campaign closed at ~1.9 ns/pair after 20 consecutive failed attempts.
The survey explains why, and it changes what to try next.

**CORRECTED 2026-08-05 by measurement (OPTLOG F16): the kernel is NOT port
bound.** Disassembly gives 45 instructions per 6 pairs and static floors of
0.78–0.94 cycles/pair against 7.2 measured — 7.7× of unused issue headroom. The
binding resource is scattered **L2 latency** over a 2.5 MB dense corpus, so the
lever is blocking the corpus to keep a working subset L1-resident, not widening
the kernel. The inference below was wrong and is kept only to mark the
correction.

~~The 6-pairs-in-flight kernel is almost certainly load-port bound, not
memory-parallelism bound.~~ Travis Downs' work on load-buffer occupancy, ROB
size and load-port throughput makes the distinction: MLP ceilings gate
outstanding *misses*, and at 632 B/row every probe is an L1/L2 **hit**. Twenty
failed attempts to widen ILP, prefetch, reorder or reshape is exactly what a
port ceiling looks like.

> **Action before any 21st iteration: get a port-pressure breakdown**
> (`llvm-mca` / the `optimize-aarch64-kernel` skill) instead of guessing. MLP is
> the wrong lever in this regime; it is the right lever only at DRAM scale,
> where the zone map already avoids most scattered access.

**The one structural idea that could break the plateau: bit-transpose to batch
singleton queries.** With median sparse-side cardinality 1, 69% of pairs reduce
to "is bit p set in row j?" for a fixed p over ~1.7 M rows. Transposing blocks so
column p becomes a contiguous bit-vector turns that scattered load into a
**linear scan**. This requires reordering the iteration of the whole all-pairs
loop — grouping pairs by shared sparse index before touching the dense side —
so it is an algorithm change, not a kernel change. It cannot help pairs where
neither side is a singleton. Prototype on a 4096-row tile.

**Ruled out, with reasons:**

| candidate | verdict |
|---|---|
| **M4RI / M4RM** (Method of Four Russians, GF(2)) | **No.** Computes an XOR-linear product, not `popcount(A AND B)`. No adaptation recovers exact AND-cardinality without doing the popcount anyway. The surface analogy is real; the semantics are not |
| **chemfp / BitBound** (Swamidass–Baldi) | **No.** Their lever is *skipping* pairs under a Tanimoto threshold. We need every pair's exact count, so there is nothing to prune and the bound degenerates to a no-op. Their inner loop is plain popcount-of-AND — the same conclusion we reached |
| **SIMD sorted-list intersection** (Schlegel, Inoue, Lemire) | **No.** Targets lists of 16+ elements. At median cardinality 1 there is no merge to vectorize |
| **PLINK2 KING** (`plink2_matrix_calc.cc`) | Not applicable, but **corroborating**: a mature genomics all-pairs tool converged on nested `PopcountWord(a & b)` with sample-major blocking and a source comment explicitly *rejecting* table-lookup batching. Independent confirmation that hardware popcount is solved |

### 13.2 LARGE regime (1.25 MB/row, DRAM-bound)

**Multi-level zone map — highest-value item still unbuilt.** BitFunnel's higher
ranks are exactly this: each rank-*i* bit is the OR of 2^i rank-0 bits, scanned
first because it is shorter and eliminates most candidates. At 10⁷ bits our
single-level zone map is 2.4 kB/row; a second level at 1 bit per 512 zone-map
bits is **~9 bytes/row**, turning a 2.4 kB AND into a 9-byte pre-check for
disjoint pairs. Given the single level already won 20.5×, this is the obvious
next multiplier at the sparse end.

**Cross-pair panel blocking (BLIS/Goto).** Still the biggest structural gap. The
survey's refinement is worth noting: at 1.25 MB/row a panel of even *one* row
saturates L2, so panels should be sized in **zone maps** (2.4 kB each) — pack
~100–400 summaries per panel and stream full rows only for surviving bins. That
is a different design from the naive "pack rows" reading of §2.

**Non-temporal hints: mostly not applicable.** AND+popcount reads two operand
streams and writes a scalar, so there is no output stream to bypass and NT
*stores* buy nothing. NT *loads* may help for the streamed (non-panel) operand
once panel blocking exists — worth a differential microbenchmark then, not now.

*(Load balancing and out-of-core scheduling findings from this survey have been
recorded for Tomahawk and are deliberately not carried here.)*

### 13.3 Sparsity range — two findings that matter

**The mid-band negative result is confirmed by the literature.** Nothing beats
bitmap-AND-popcount between ~0.4% and ~99.6% density for cardinality-only.
Roaring's own array↔bitmap threshold (4096 per 2¹⁶ container) sits in the same
place by independent empirical design, and it has no third container for the
middle. Partitioned Elias-Fano and bit-sliced layouts target different problems
(skewed sorted lists; multi-attribute filtering) and degrade toward bitmap cost
as density rises. **Our negative result stands and is well-supported.**

**The complement/dense-tail strategy appears to be genuinely unclaimed.** No
library or paper systematically stores or computes on the complement when
density > 0.5 to accelerate *intersection cardinality*. EWAH represents 0-runs
and 1-runs symmetrically at the representation level and Roaring exposes `flip`
for range construction, but neither presents `|A∩B| = |A| + |B| − |A∪B|` or a
De Morgan re-expression as a cardinality *algorithm*. Our measured 52.7× at the
dense tail has no prior citation to anchor it — **write it up as a contribution
rather than expecting one.**

### 13.4 ISA-specific, actionable

- **SVE2 `MATCH` / `HISTCNT` for the S×S cell.** Vardanian's measured 3–5.6× on
  sorted arrays (Graviton4). Structurally a sorted-array technique — it does not
  touch bitmaps — so it applies to S×S only. We have SVE2 hardware.
- **VP2INTERSECT emulation** (Diez-Cañas, arXiv:2112.06342) beats Intel's own
  microcoded instruction, and the paper notes computing only **one** output mask
  is cheaper — which is exactly our cardinality-only case. Intel removed the
  instruction after Tiger Lake; **AMD Zen 5 added it natively**.
- **`VPCOMPRESS` is a representation-conversion primitive** (B→S), not a kernel
  speedup. **`VPCONFLICT` has no cardinality use case** — drop it from §4's D3.
- **Zen 4 landmine:** `_mm_mask_compressstoreu_*` costs ~256 µops on Zen 4 vs ~6
  on Ice Lake (fixed in Zen 5). Compress to register, then store separately.
- **CRoaring runtime-gates AVX-512 on microarchitecture, not just the feature
  bit**, because of historical SIMD frequency throttling. Match that practice.
- **RVV: no published set-intersection kernel exists.** An open gap; do not cite
  any RVV throughput number, none exists.
- **Lemire (2026-06):** AVX2 register widening alone gives a **measured 22%** on
  pairwise intersection cardinality in Roaring, while `popcnt` vs software
  popcount is ~43% — a citable x86 baseline for the B×B path.

### 13.5 A sourcing gap that will not close

**No M2/M3/M4 microarchitecture corpus exists anywhere.** Dougall Johnson's
`applecpu` covers M1 Firestorm/Icestorm only and has not been updated since
2023-07. Every M4 claim in this project must therefore go **tier-1 → tier-3
directly**, skipping tier-2 entirely, because there is nothing to source. The
Firestorm figures we have used as hypotheses are M1 and must stay labelled as
such.


---

## 14. The paper — specification, and what each claim still needs

Written 2026-08-04 to drive the remaining work. Every item below is either
**MEASURED** (evidence exists in `results/`), **PARTIAL**, or **MISSING**. The
plan from here is to close the MISSING rows in priority order, not to keep
optimizing.

### 14.1 What the paper is

**Not** "a faster library for all-pairs set intersection." The individual
mechanisms mostly have prior art (§12): zone maps are BitFunnel, rank/select is
Jacobson, probe-and-commit is Micro Adaptivity, adaptive format selection is
EmptyHeaded/IA-SpGEMM. Claiming those would be found out.

**The paper is an empirical study**, and its contribution is the *map*:

> **When is work worth avoiding rather than accelerating, in all-pairs boolean
> intersection?** A systematic measurement of ten representation pairings across
> the full density range, three orders of universe size, and four
> microarchitectures — with the finding that vectorization wins in exactly one of
> the ten cells, and that the two strategies scale in opposite directions with
> the memory hierarchy.

Target: arXiv preprint → *Software: Practice and Experience*. Same venue family
as Roaring (SPE) and the popcount papers.

### 14.2 Claims, and their evidence status

| # | Claim | Status | Gap |
|---|---|---|---|
| **C1** | Across ten representation pairings and five corpus shapes, a SIMD-throughput kernel wins **0 of 25** asymmetric measurement points | **MEASURED** | — |
| **C2** | As the working set grows L2→DRAM, **vectorization's advantage decays on every microarchitecture** (1.52→1.17 M4, 1.56→1.03 SVE2, 5.38→1.55 SPR) while work avoidance is maintained or grows (8.8→20.5, 12.4→22.8, 15.3→14.8). **The gap widens everywhere** | **MEASURED, 3 ISAs** (F17) | Original "opposite directions" wording is only half-supported — the growth half is ARM-only, flat on Sapphire. Reworded to the relative claim, which holds on all three |
| **C3** | The cost curve is **U-shaped in density**; the operative variable is distance from ½, not sparsity | **MEASURED** | Dense tail measured only via R×R incidentally — see C4 |
| **C4** | Computing on the **complement** above density ½ is a systematic strategy for intersection cardinality, and is unclaimed in the literature | **MEASURED** (F13) | Cell built (`cell_comp.cpp`), oracle-tested, crossover measured at complement density ~0.1–1%, mirroring the sparse side's ~0.4%. **Win over the accidental R×R is only ~20%** — the contribution is that the strategy is now selectable and statable, not that it is much faster. State it that way |
| **C5** | A rank index makes B×R cost Θ(runs), independent of run length | **MEASURED** | 1 host; crossover ~256 bits |
| **C6** | Adaptive pairing beats a `run_optimize`-tuned CRoaring by 1.7–19.1× | **SUPERSEDED by §15** — restated on real corpora as **1.09–15.83×, 16/16**, including CRoaring's own default benchmark data | Synthetic 4-ISA figure retained as a secondary result; the real-corpus figure is 1 ISA and needs the other three |
| **C7** | Per-pair selection costs 28–48% of runtime; tile hoisting brings it to 0.10–0.29%, and probe-and-commit has **59.0% regret** against a bucket oracle vs all-bitmap's 118.0% | **MEASURED** (F15) | Regret is a *lower bound* (bucket oracle, not per-pair — the latter is below clock granularity). Neoverse V1 still absent from the Gate-1 table |
| **C8** | ~~The asymptotic win requires a large universe **and** sparsity **simultaneously**, and no public dataset we can reach has both~~ | **REFUTED by §15.** The first clause stands; the second was false | The clause "no public dataset has both" was an over-generalisation from a single *genomic* corpus. Graphs, web-attribute and census data reach the regime routinely: `uscensus2000` (m=3.7e7, d=8.1e-7), `as-skitter` (m=1.7e6, d=9.1e-6), `com-LiveJournal` (m=4.0e6, d=5.1e-6). Restated correctly: **human variant data specifically gives a large universe *or* sparsity, never both** — genomics sits mid-band and is a scoping remark, not a limit on the method |
| **C9** | Negative results: Harley-Seal (0.38–0.72×), D2 run-collapsing (0.25–0.51×), register blocking (refuted at L2 *and* DRAM), prefetch, NEON index arithmetic | **MEASURED** | — |
| **C10** | At small universes the binding constraint is per-pair overhead, not kernel work; moving decisions per-pair→per-row gives 2.9×. The residual is **scattered L2 latency, not issue width** | **MEASURED** (F16) | Disassembly: 45 instructions / 6 pairs, floors of 0.78–0.94 cyc/pair against 7.2 measured. The "port-bound" reading in §13.1 was **inferred and is wrong** |

### 14.3 Figures, and whether they exist

| fig | content | status |
|---|---|---|
| **F1** | Density sweep, all ten cells, log-log, with the fixed-cost B×B line | **EXISTS** — `results/density.png` |
| **F2** | Speedup vs density, showing both tails and the mid-band where B×B wins | **EXISTS** — same figure, panel 2 |
| **F3** | Winning variant per cell per density (the selection map M2 must reproduce) | **EXISTS** — same figure, panel 3 |
| **F4** | **C2**: SIMD vs work-avoidance advantage vs working-set size | **EXISTS** — `results/paper_figures.png` panel 1 |
| **F5** | **C6**: Storm vs CRoaring across density, one line per microarchitecture | **EXISTS** — panel 2. Peaks 6.5× / 6.7× / 13.8× / **19.1×** |
| **F6** | **C5**: B×R cost vs run *length* at fixed run *count* | **EXISTS** — panel 3. No-index grows 18.5×, indexed flat |
| **F7** | **C8**: speedup vs universe size, real data at both orientations plus the synthetic sweep | **DATA EXISTS** (F14) — plot pending. Must show the two real points (2.15× at 5 kbit sparse, 0.93× at 1 Mbit mid-band) against the synthetic curve |
| **F8** | Real-data anchor: 1000 Genomes allele-frequency spectrum vs the generator's | **PARTIAL** — spectrum measured, not plotted, generator not overlaid |

### 14.4 Ordered plan to close the gaps

1. **C4 — build the complement representation.** A first-class `C` (complement)
   representation with its own pairing rules, so the dense tail is a *deliberate
   strategy* rather than R×R getting lucky. This is the paper's strongest
   novelty claim and it currently rests on an accident. Also gives `|A∩B|` via
   De Morgan a clean formulation to state.
2. **C8 / F7 — measure the large-universe regime on real data.** Concatenate
   several 1000 Genomes chromosomes to reach 10⁶–10⁷ bit universes, or use the
   variant axis as the universe. Without this the headline claim is extrapolated
   and a reviewer running chr20 gets 2.15×.
3. ~~**F4, F5, F6 — plot data that already exists.**~~ **DONE** —
   `bench/plot_paper.py` → `results/paper_figures.{png,svg}`.
4. ~~**C7 — regret for the probe-and-commit policy.**~~ **DONE** (F15).
   Probe-and-commit 59.0%, per-tile 106.7%, all-bitmap 118.0%, per-pair model
   **403.7%** — the model alone is worse than no selection. Nothing is within
   1.5× of the oracle, so selection is not solved, only no longer harmful.
5. ~~**C10 — port-pressure trace.**~~ **DONE** (F16), and it **refuted** the
   port-bound hypothesis: 7.7× of unused issue headroom, so the residual is
   scattered L2 latency and the lever is corpus blocking, not kernel width.
6. ~~**C2 — repeat the residency sweep on Neoverse and Sapphire.**~~ **DONE**
   (F17). The SIMD decay replicates on all three and is sharpest on Sapphire
   (5.38→1.55×); the zone-map *growth* is ARM-only and flat on x86, so the claim
   was reworded from "opposite directions" to "the gap widens everywhere".

Items 1 and 2 are what change the paper's standing. Items 3–6 are what stop a
reviewer from rejecting it on rigor.

### 14.5 Status, 2026-08-05 — all six closed

| # | gap | outcome |
|---|---|---|
| 1 | C4 complement cell | **Closed.** Built and oracle-tested; win over the accidental R×R is ~20%, so the contribution is that the strategy is *selectable and statable*, not that it is fast |
| 2 | C8 large universe on real data | **Closed, and it cost the headline.** Haplotype-major chr20 is 1 Mbit/row at 3.14% density — **nothing beats all-bitmap (0.93×)**. Real data gives a large universe *or* sparsity, never both. The 10²–10⁵× regime is synthetic-only |
| 3 | F4/F5/F6 figures | **Closed.** `results/paper_figures.png` |
| 4 | C7 regret | **Closed.** Probe-and-commit 59.0% vs all-bitmap 118.0%; the per-pair model is 403.7%, worse than no selection |
| 5 | C10 port trace | **Closed, and it refuted the hypothesis.** 7.7× of unused issue headroom — not port-bound; the residual is scattered L2 latency |
| 6 | C2 off-host | **Closed, and it weakened the claim.** SIMD decay universal; zone-map growth ARM-only |

**Three of six closures made the paper weaker rather than stronger** (2, 5, 6),
and one (1) delivered far less than hoped. That is the point of running them.
The claims that survive are the ones worth publishing, and they are now stated
at the strength the evidence actually supports.

---

## 15. Real-corpus run, 2026-08-05 — 1:1 overlap with the Roaring papers

Full record: [`results/CORPORA.md`](results/CORPORA.md). Raw output:
`results/corpora/`. Reproduce: `bench/run_corpora.sh`, collate with
`bench/summarize_corpora.py`.

### 15.1 What was run and why

Every Storm-vs-CRoaring number before this came from our own generator, which
made C6 unfalsifiable in principle: a generator can encode the structure the
kernels exploit. This run uses the **twelve `real-roaring-datasets` corpora that
CRoaring's own harness runs by default** (its README names `census1881` as the
default), plus four larger modern graphs. `tools/sets2bin.py` reproduces
census1881's published statistics exactly (m=4,277,806, mean |Xi|=5,019.3,
matching arXiv:1603.06549), which validates the loader against the source paper.

`_srt` variants are the row-sorted versions the Roaring papers report
separately, giving two clustering points per corpus.

### 15.2 Result

**17 of 17 corpora: Storm's best cell beats `run_optimize`-tuned CRoaring.**
Range **1.09×–15.83×**, median ≈5.6×. Density spans 8.1e-7 to 1.7e-1 and
universe spans 2.0e5 to 3.7e7 across 17 corpora.

Winning-cell distribution: **B×B ×10, B×S ×4, B×R ×1, S×S ×1, R×R ×1.**

That distribution is the result, more than any ratio. Had one column dominated,
the pairing matrix would be unnecessary and the paper would refute itself. The
winner migrates with density — B×R / S×S / B×S at the sparse end, B×B at the
dense end — and the losing columns lose badly (W×W is below 1.0× on 16 of 17).

### 15.3 New claim C11 — Roaring's container threshold is tested on the wrong statistic

The dense-corpus wins (weather_sept_85 10.75×, census-income 12.82×) initially
looked like a measurement error: at 6–17% density both sides should be doing
near-identical popcount work. A container census via
`roaring_bitmap_statistics()` after `run_optimize()` shows why they are not.

| corpus | array | bitset | run | global density |
|---|---:|---:|---:|---:|
| weather_sept_85 | **2,274** | 561 | 21 | 6.3e-02 |
| census-income | **553** | 180 | 35 | 1.7e-01 |
| census1881 | **1,332** | 0 | 132 | 1.2e-03 |
| wikileaks-noquotes | 199 | 0 | **1,693** | 1.0e-03 |
| uscensus2000 | **2,219** | 0 | 2 | 8.1e-07 |

Roaring picks its container by testing **per-2^16-chunk cardinality against a
fixed threshold of 4096**. With a large universe the mass spreads thinly enough
that individual chunks stay under that threshold even when *global* density is
6% or 17% — so CRoaring runs array merges where Storm popcounts. census1881 has
**zero** bitset containers at density 1.2e-3.

This is the fixed-chunk/fixed-threshold weakness the pairing-matrix argument
targets, **observed directly rather than inferred**. It is a "do the right kind
of work" result, not a SIMD result — consistent with C1.

**Evidence tier 3 (measured).** Threshold value and container semantics are
tier 2 (CRoaring v4.7.2 source, vendored at `third_party/croaring_amalg`).

### 15.4 Consequences for the paper

1. **C8's second clause is withdrawn.** "No public dataset has both" was
   generalised from one genomic corpus and is false. Genomics is mid-band; that
   is now a one-line scoping remark, not a wound. The 10²–10⁵× regime is **no
   longer synthetic-only** — `uscensus2000` measures 23,080× over all-bitmap.
2. **The publish-alone decision is settled.** Standalone, Roaring-paper-shaped
   (§8b unchanged: Storm then Tomahawk). The bundling question was conditioned
   on this experiment failing; it did not fail. See §15.5.
3. **Positioning must be precise about Roaring.** Roaring *already* dispatches
   pairwise on container types. The contribution is not "dispatch on
   representation pairs" — it is (a) bin-resolution zone maps, 128× finer than
   Roaring's 2^16 chunk; (b) adaptive rather than static selection
   (probe-and-commit, Gate 1 at 0.10–0.48%); (c) representation chosen per
   row/tile over the whole universe rather than per fixed chunk; (d) the
   measured density map. C11 is the sharpest evidence for (a) and (c).

### 15.5 Bundle-vs-standalone — resolved

Decision rule set by the author: bundle with Tomahawk **iff** a real dataset in
the target regime could not be found. One was found — sixteen were. **Standalone.**

Tomahawk (`/Volumes/SabrentM2/ML/projects/science/tomahawk`, ~21k LOC, 50
commits, dormant since 2019-04-25) is prior art by the same author and already
implements list / bitvector / EWAH / runlength dispatch (`lib/ld/ld_structs.h`,
`ld_engine.cpp`). It is an **application** of the result and a citation, not a
competitor and not a co-paper. Bundling would bury the kernel work in a Methods
section and force genomics — the one measured mid-band domain — to carry the
entire evaluation.

### 15.6 What this run does not establish

1. **One microarchitecture.** Apple M4 only. Zone-map growth is known ARM-only
   and flat on Sapphire (C2), so the x86 numbers will differ and must be
   measured, not extrapolated. Rerunning on `fpga-neo1` / `fpga-neo2` /
   `fpga-sapphire` is the top remaining task.
2. **17/17 stands; the "15/17" caution was wrong.** `dimension_003` and
   `uscensus2000` were provisionally called ties on the guess that noise had
   lifted them above 1.0. Independent repeats refute it — dimension_003 returns
   1.21–1.44 (one outlier at 3.72), uscensus2000 returns 2.05–3.29, i.e. every
   repeat exceeds the recorded 1.09× and 1.39×. Both are genuine wins.
3. **No error bars anywhere, and the variance is large.** Repeats span 16.1–18.2×
   (census1881), 7.6–13.9× (census-income), 1.2–3.7× (dimension_003) — worst on
   the fastest corpora, where a 20,000-pair batch lasts only ~58 µs. The bias is
   also **systematic**: every standalone repeat checked exceeds its batch-run
   value, implicating thermal accumulation and cross-corpus cache pollution
   across the 17-corpus sequential run. Quote the ordering, not the digits, until
   round-robin interleaving and median/IQR reporting land.
4. **Selection cost excluded** — cells are timed directly. End-to-end numbers
   including selection have not been rerun on these corpora.
5. **`vs all-bitmap` is a weak baseline** at large universe (104 MB for
   census1881 at 200 rows). The CRoaring column is the defensible one.

### 15.7 Next

1. Cross-ISA rerun of §15 on all four hosts.
2. Error bars; downgrade the two marginal wins to ties if they do not survive.
3. End-to-end including selection cost.
4. `UShER SARS-CoV-2` — 8.45M genomes, open, 678 MB variant-major VCF; the one
   real *genomic* dataset reaching the regime. Density unmeasured.
5. msprime/stdpopsim scale sweep, replacing the hand-rolled 1/i draw in
   `kernels/storm_gen.cpp` with a coalescent generator.

### 15.8 Zone-map ablation, 2026-08-05 — new claim C12

Full table: [`results/corpora/ABLATION.md`](results/corpora/ABLATION.md), §7 of
`results/CORPORA.md`. Collate with `bench/ablate_zonemap.py`.

The zone map is built **unconditionally** in `build_row()`, so §15.2's table
could never answer whether it is worth building. `bench_baseline` now times each
cell's best index-free variant against its index-using one. The index-free B×B
baseline is `neon_u8`, not the portable `dense` — comparing against the weaker
portable form would have inflated the mechanism on ARM for the wrong reason.

**C12: the zone map helps 12/17 corpora, is neutral on 5, harms 0 at
system level, and costs 0.195% of bitmap bytes. Net gain 1.00×–3.80×.**

Three qualifications that matter more than the headline:

1. **The per-cell B×B figure (up to 745×) must not be quoted as the benefit.**
   Those are corpora where B×B loses to a sparse cell regardless; the zone map
   is rescuing a kernel that would never be selected.
2. **B×S with a zone map is below 1.0× on 10 of 17.** `bs_occ` loses to plain
   `ilp8` — when the sparse side is a short list, gating each 512-bit bin costs
   more than the probes it saves. Clean negative result in the cell where the
   mechanism looked most attractive.
3. **It never loses at system level only because selection routes around it.**
   Per-cell it can lose badly (B×S on census1881, 0.25×). So the zone map is not
   independently a good idea — it is a good idea *given* a selector that can
   decline it. State that coupling explicitly; it is a claim about the
   architecture, not about the data structure.

The five neutral corpora are exactly those whose winner is a sparse cell reading
no side structure. Gains cluster at both ends of the density range with a
neutral middle, and the `_srt` variants gain consistently more than their
unsorted counterparts (3.80 vs 2.49; 3.48 vs 2.21; 2.80 vs 1.18) — the expected
direction, since sorting raises clustering and clustering is what empties bins.

**Open:** the build cost of `build_occ()` is not charged anywhere in these
numbers — rows are constructed before timing starts. At 0.195% space it is
unlikely to matter, but the claim is currently "free to use", not "free to
build", and only the former is measured.

### 15.9 The workload is emptiness-proving — measured, and it reframes the stack (C13)

Measured on the identical pair sample the timings use (`bench_baseline` now
reports `# DISJOINT`), 20k stride-sampled pairs per corpus:

| corpus | disjoint pairs | mean \|A n B\| |
|---|---:|---:|
| dimension_003 | **100.0%** | 0.00 |
| uscensus2000 | **100.0%** | 0.00 |
| com-LiveJournal | **99.9%** | 0.00 |
| soc-Pokec | **99.9%** | 0.00 |
| com-Orkut | **99.5%** | 0.01 |
| as-skitter | **99.4%** | 0.02 |
| census1881 | 96.9% | 0.76 |
| wiki-Talk | 95.1% | 0.11 |
| weather_sept_85 | 34.7% | 4025.78 |
| census-income | 24.8% | 5927.27 |

**C13: in the target regime, 95-100% of pairs have an empty intersection.** The
operation being optimised is therefore *disjointness proof*, not intersection.
This is not a sampling artifact -- it is what all-pairs means on a large sparse
universe, where E[|A n B|] ~ |A||B|/m is 1.3e-4 for as-skitter.

Consequences:

1. **It re-explains the headline.** The 2-16x over CRoaring is largely a measure
   of how cheaply each cell reaches a zero, not how fast it counts.
2. **It re-explains the C12 ablation.** The zone map gives B x B up to 745x
   because `occ_A & occ_B == 0` exits immediately; B x S gains nothing because
   it already short-circuits, just expensively.
3. **The dense corpora are a genuinely different workload** (25-35% disjoint,
   mean |A n B| in the thousands) and should be reported as such rather than as
   the same experiment at another density.

**Caveat on generality:** this is uniform all-pairs. A *windowed* workload -- LD
within a genomic window, or neighbours-of-neighbours -- deliberately selects
correlated pairs and would have a far lower disjoint rate. Tomahawk's windowed
mode is exactly that case. The 95-100% figure belongs to all-pairs and must be
labelled that way.

### 15.10 Blocked Bloom filter above the zone map — analysis before building

Proposal: Bloom -> zone map -> data, so that most pairs are discarded at O(1).

**The O(1) filter-AND test is sound but weak here.** If every element sets k bits
then any x in A n B has all k bits set in both filters, so
`popcount(bloomA & bloomB) < k` *proves* disjointness with no false negatives.
The problem is calibration: to make that test fire often for |A|=|B|=15 you need
the filters sparse enough that random overlap stays under k, which needs roughly
|A||B|/FPR bits -- about 2,250 bits for a 10% false-positive rate. The zone map
for as-skitter is 3,316 bits. **The O(1) test buys no space over what we already
build**, because both are bounded by the same information-theoretic content.

**The real win is cache residency, not asymptotics.** B x S currently probes a
212 kB bitmap |B| times: ~15 scattered L2/DRAM accesses to establish a zero.
The same 15 probes against a 512-byte per-row Bloom are L1 hits. At 4,096 bits,
k=2, |A|=15, the false-positive rate per probe is (30/4096)^2 ~ 5.4e-5, so
essentially every one of the 99.4% disjoint pairs exits from L1 and never
touches the bitmap. That is the mechanism worth building: **not "AND the
filters", but "probe a small L1-resident filter instead of a large DRAM one".**

Predicted effect: the win should track the ratio of bitmap footprint to filter
footprint, so it should be largest on `uscensus2000` (m=3.7e7, 4.6 MB/row, 100%
disjoint) and absent on `census-income` (m=2.0e5, 25% disjoint, already L2).

**Open question the analysis cannot settle:** whether a hashed Bloom beats a
positional zone map *of the same size*. Hashing spreads elements uniformly;
the zone map's bins are positional, so clustering makes whole bins empty and
helps it, while scattering hurts it. as-skitter is partly clustered (117 run
containers vs 83 array). This is an empirical question, and the honest
experiment is Bloom vs a zone map truncated to the same byte count -- not Bloom
vs the current full-size zone map, which would confound size with structure.

**Prior art to cite, not re-invent:** Bloomjoin / semi-join reducers (Mackert &
Lohman, VLDB 1986); blocked Bloom filters (Putze, Sanders, Singler, JEA 2009);
BitFunnel's higher-rank rows (Goodwin et al., SIGIR 2017) are already the
closest structure to the zone map.

**Status: tier 1 (analysis only). Nothing here is measured.** The disjointness
rates in 15.9 are tier 3; every claim in 15.10 is a prediction.

### 15.11 Tier-0 filter experiment — the Bloom hypothesis is refuted (C14)

Run: `bench/bench_bloom.cpp`. Results: `results/filter/`. 15 repeats,
round-robin interleaved across variants inside each repeat so drift hits all
variants equally. All variants exact — the filter only skips probes it can prove
absent, every survivor is confirmed against the bitmap. Filter budget 16,384
bits (2 kB/row) for the table below; a 512–65,536-bit sweep is in
`results/filter/`.

**The fairness control was the whole experiment.** Comparing a Bloom filter
against the existing 512-bit-bin zone map confounds size with structure, so
every Bloom of N bits was raced against a **coarse positional zone map of
exactly N bits** — same footprint, same cache behaviour, differing only in
hashed vs positional bucketing.

| corpus | disjoint | bitmap/row | ilp8 ns | bloom | blocked bloom | **coarse zone map** | existing zone map |
|---|---:|---:|---:|---:|---:|---:|---:|
| com-LiveJournal | 99.9% | 493 kB | 25.5 | 2.73x | 2.26x | **3.70x** | 1.81x |
| soc-Pokec | 99.9% | 199 kB | 36.3 | 2.82x | 2.21x | **3.46x** | 1.64x |
| com-Orkut | 99.5% | 375 kB | 92.7 | 2.64x | 1.86x | **3.05x** | 1.41x |
| as-skitter | 99.4% | 207 kB | 10.4 | 1.60x | 1.35x | **2.08x** | 0.93x |
| uscensus2000 | 100.0% | 4,514 kB | 6.2 | 1.62x | 1.44x | **2.08x** | 1.06x |
| wiki-Talk | 95.1% | 292 kB | 25.1 | 1.53x | 1.36x | **1.38x** | 0.87x |
| dimension_003 | 100.0% | 472 kB | 8.1 | 0.71x | 0.52x | **1.20x** | 1.11x |
| census1881 | 96.9% | 522 kB | 97.8 | 0.31x | 0.23x | **0.63x** | 0.28x |
| census-income | 24.8% | 24 kB | 1894.5 | 0.22x | 0.16x | **0.31x** | 0.32x |
| weather_sept_85 | 34.7% | 124 kB | 2866.8 | 0.25x | 0.18x | **0.29x** | 0.29x |

**C14, three parts, all tier 3 (measured):**

1. **The Bloom filter loses to a size-matched positional zone map on 9 of 10
   corpora.** The hypothesis in 15.10 was wrong. Hashing spreads elements
   uniformly and thereby *destroys* the positional clustering that makes
   bucketing effective; the coarse zone map keeps it. Selectivity is not the
   reason — Bloom is consistently the *more* selective filter (3.1% survival vs
   5.8% on uscensus2000) and still loses, because its probe costs two multiplies
   and two loads against the zone map's one shift and one load. **Cheaper probe
   beats better selectivity.**
2. **Blocked Bloom is worse than plain Bloom on 10 of 10.** At a 2 kB budget the
   whole filter spans ~32 cache lines, so confining two probes to one line saves
   little, while the extra block-selection hash costs on every probe. Blocking
   is a technique for filters much larger than cache; it is counterproductive
   here.
3. **The current 512-bit-bin zone map is the wrong size.** A *fixed* 2 kB budget
   beats the *m/512-proportional* map on 8 of 10 corpora — 3.70x vs 1.81x on
   com-LiveJournal, and 2.08x vs 0.93x on as-skitter where the existing map
   actively loses. Scaling the summary with the universe is the defect: at
   m=3.7e7 the existing map is 9 kB and no longer cache-resident, which is the
   thing that was supposed to make it cheap.

**Where it fails, and why.** All filters lose on `census1881` (0.63x),
`census-income` (0.31x) and `weather_sept_85` (0.29x). The predictor is not the
disjoint rate — census1881 is 96.9% disjoint and still loses. It is **|S|**: the
filter adds one probe per list element on top of the existing work, so it pays
only when |S| is small relative to the bitmap. census1881 has mean |Xi|=5,019
and weather 64,353. The rule is roughly "wins when ilp8 is already fast
(6–93 ns), loses when it is slow (98–2,867 ns)" — the filter is a latency
optimisation, not a throughput one.

**Consequence for the architecture.** This is not a new cell; it is a new
*decision*, and it goes the same way as C12: the tier-0 filter is a good idea
only where the selector can decline it. Filter width now joins representation
choice as something the cost model must set, and it has a clean predictor
(|S| vs bitmap footprint) that should be cheap to evaluate per tile.

**Not yet done:** filter build cost is not charged (built before timing, like
`build_occ`); one microarchitecture; the 512–65,536-bit sweep is visibly noisy
and the 2 kB optimum should be re-established with error bars before it is
quoted as a tuned value.

### 15.12 Optimizing the zone map — two ideas tried, both lost (C15)

Run: `bench/bench_bloom.cpp`, 15 repeats, round-robin interleaved. Baseline is
`B x S ilp8`, no filter.

**Headline, with error bars.** Coarse positional zone map at a **fixed 16,384-bit
(2 kB) budget**, 9 independent processes per corpus, 9 interleaved repeats each:

| corpus | median | IQR | min–max |
|---|---:|---|---|
| com-LiveJournal | **4.03x** | [3.75, 4.31] | 3.57–5.02 |
| com-Orkut | **3.62x** | [3.46, 3.83] | 3.41–3.89 |
| as-skitter | **3.62x** | [2.75, 3.93] | 2.48–3.96 |
| soc-Pokec | **3.51x** | [3.10, 3.77] | 3.00–3.84 |

No IQR comes near 1.0, so the effect is real at ~3.5–4x on the
emptiness-dominated graph corpora. This is the first number in the project
reported with a dispersion estimate rather than a best-of-N point.

**Attempt 1 — size the filter to cardinality instead of a fixed budget. FAILED.**
Width `clamp(next_pow2(32*|A|), 512, 16384)` gives 82–317 B/row instead of
2 kB, and loses on 7 of 9 corpora (as-skitter 3.34x vs 4.69x; uscensus2000
2.67x vs 3.73x). The reasoning behind it was wrong: **survival rate dominates,
not filter footprint.** Both variants are cache-resident, so shrinking the
filter buys no cache benefit and costs selectivity — for as-skitter the adaptive
map has 512 buckets vs 16,384, so survival rises from 0.09% to 2.9%, roughly 32x
more fall-throughs, and every fall-through is a genuine DRAM miss into a
207 kB/row bitmap. Cheap-to-hold beats small.

**Attempt 2 — collapse consecutive same-bucket probes. FAILED badly.** The list
is sorted, so neighbouring elements often share a bucket; probing once per
distinct bucket and skipping the group should have helped exactly where the
filter was losing (large |S|). It made things worse everywhere: 2.45x vs 3.73x
on uscensus2000, and **0.13x vs 0.41x on weather_sept_85**. The group-scan
`while (j < n && (S.v[j] >> shift) == b) ++j` is a serial dependent loop with a
data-dependent branch per element, which is worse than the branchless
shift-load-test it replaces. Branch-free per-element probing wins even when it
does strictly more lookups.

**Attempt 3 — tune the width.** Optimum is 16,384–32,768 bits (2–4 kB) on the
graph corpora (com-Orkut 3.69x/3.75x at 16k/32k, falling to 2.59x at 256k;
com-LiveJournal peaks at 16k). `uscensus2000` and `as-skitter` are too noisy to
call — adjacent sizes swing 2.69x to 5.11x. **Finer tuning is not meaningful
with this harness**, which is the third time run-to-run variance has blocked a
conclusion (cf. 15.6, 15.11).

**C15, the design rule.** Make the filter **as large as stays cache-resident,
with a branchless single-load probe**, and let the selector disable it when |S|
is large. Every attempt to be cleverer — hashing (C14), adaptive sizing,
group-collapsing — lost to that. The two failures share a cause: they traded a
cheap predictable operation for a smarter expensive one, and at 3–30 ns/pair the
cheap predictable operation is the whole budget.

**Still open:** filter build cost remains uncharged; one microarchitecture; and
the width optimum is stated as a range (2–4 kB) rather than a tuned value
because the harness cannot resolve finer.

### 15.13 Making the zone map generic — online gate and offline optimizer (C16)

Two products, deliberately different, as with Roaring's runtime containers vs
`run_optimize()`:

1. **Online gate** — data unknown in advance; decide per tile from a sample.
2. **Offline corpus optimizer** — data in hand; spend time once, store the
   configuration.

Run: `bench/bench_bloom.cpp` (`--optimize` for mode 2), `bench/gate_eval.sh`
for medians over independent processes. Results `results/filter/`.

#### The problem C15 left

The 2 kB coarse map was worth 3.5-4x on sparse graphs and **0.39x on
weather_sept_85, 0.45x on census-income**. Halving throughput on a third of the
corpora makes it undeployable however good the best case.

#### (1) Online gate — result

| | always-on | **gated** |
|---|---:|---:|
| geomean over 17 corpora | 1.194x | **1.486x** |
| worst case | **0.39x** | **1.00x** |
| corpora harmed (<0.98x) | 8 | **0** |

Gating both raises the mean and removes every regression; the filter is selected
on 9 of 17. Rule:

```
use_filter  <=>  survival < 0.15  AND  touch < 0.25      (width fixed at 16,384 bits)
```

`survival` = fraction of sampled probes the filter fails to reject.
`touch` = |S| divided by the dense side's cache-line count.

**Both terms are necessary and neither is derivable statically.** Fill rate does
not predict survival — `dimension_008` has fill 0.0002 and survival 0.933,
because the sparse side's elements land precisely in the dense side's occupied
buckets; correlated data defeats any closed-form estimate. And a selective
filter still loses when |S| is large enough that bitmap probes stop being
random: `dimension_033` has survival 0.063 but touches 2.85 lines per line of
bitmap, which the prefetcher already streams.

**The thresholds are plateau centres, not fitted points.** Every combination
with `survival in [0.10, 0.25]` and `touch in [0.20, 0.50]` gives geomean
1.503-1.508 at worst case 1.00. Flatness across a 2.5x range in each parameter
is the evidence the gate is not tuned per dataset. Gate cost measured over 2000
iterations: **0.07-2.53 ns/pair amortised, 0.08-1.2% of runtime**, inside the
2% Gate-1 budget.

#### (2) Offline corpus optimizer — result

Searches {bypass, 4k..256k bits} by **measured** time on the corpus, so it
cannot be wrong about its own objective. It selects widths far larger than the
online gate's fixed 16,384 — commonly 32k-262k — and bypasses on the dense
corpora, agreeing with the gate there.

Its advantage over the online gate is **smaller than expected**: geomean 1.846
vs 1.845 on the corpora where both engage, i.e. within run-to-run noise. The
real difference is coverage — it finds usable configurations on two corpora the
online gate bypasses (`wiki-Talk` 1.39x, `dimension_033` 1.07x) because it can
reach widths the gate does not consider.

#### C16, and a negative result that cost the most effort

**Probe-and-commit is WORSE than the two proxies here, which was not expected.**
Timing candidates on a sample and committing the winner — the project's own
established mechanism (M3, Micro Adaptivity) — systematically overstates wide
filters: across ~1,000 repeated sample pairs the filter set stays cache-
resident, while across the full 20,000 pairs bitmap traffic evicts it.
`wiki-Talk` measured fast on every sample and ran at **0.45-0.58x** for real. No
adoption margin up to 1.33x repaired it; the bias is structural, not noise.
Three successive fixes each repaired one corpus and mispredicted another
(survival-only ran away to the widest filter; a width cap fixed dimension_003
but left wiki-Talk; building filters for all rows rather than sampled rows
helped but did not close it), which is the signature of modelling the wrong
quantity. **The static proxies win because they measure a property of the
corpus, not of the sample's cache state.**

**Sampling bugs found and fixed**, both previously seen in this project:
the gate first sampled `pairs[0..16]`, which in a strided upper-triangle walk
all share `i=0` — it read as-skitter at survival 0.234 against a true 0.022 and
bypassed a 3x win; and the sample was sized in *pairs* when at |S|=6 that is
~190 probes, far too few, so it is now sized in **probes** (>=4096).

**Limitations:** one microarchitecture; `dimension_008` and `wiki-Talk` forgo
~1.15x by bypassing, though both measured 0.82x and 0.53x on other runs and sit
inside the noise band; as-skitter's survival varies 0.050-0.137 against a 0.15
threshold and is the closest to flipping; filter *build* cost is still not
charged, as with `build_occ`.

### 15.14 Exploiting the all-pairs structure — width pass then depth (C17)

Everything through 15.13 optimises a single pair. The workload is all-pairs and
95-100% of pairs are empty (C13), so the dominant cost is proving disjointness
N^2/2 times. `bench/bench_tile.cpp` works on tiles of T=64 rows and all 2,016
pairs within, which is the shape M3 tile hoisting already assumes.

#### Width pass — five angles, measured before choosing

Speedup over unfiltered `bs_ilp8`, per-tile build charged:

| corpus | gated (incumbent) | range | hoist | matrix | group |
|---|---:|---:|---:|---:|---:|
| com-LiveJournal | 5.89 | 5.92 | 6.19 | 7.61 | **7.78** |
| com-Orkut | 4.10 | 4.44 | 4.23 | 4.11 | **4.66** |
| soc-Pokec | 6.18 | 6.23 | 6.16 | **7.38** | 5.74 |
| as-skitter | **3.96** | 3.52 | 3.40 | 3.79 | 3.58 |
| uscensus2000 | 6.73 | 7.02 | 6.60 | 12.53 | **13.45** |
| wiki-Talk | 2.25 | 2.17 | **2.50** | 2.17 | 1.89 |
| dimension_003 | 1.22 | **48.44** | 1.24 | 13.62 | 2.40 |

**`range` -- two integer comparisons on stored [min,max] -- gives 48x on
dimension_003**, the largest single number in this project, because Druid
dimension columns are positionally clustered so most row extents cannot meet.
`matrix` and `group` roughly double the incumbent on uscensus2000. `hoist` is
marginal and was dropped.

#### Depth — five iterations, three improvements and two failures

1. **Naive composition FAILED.** range x matrix ran 13.8x on dimension_003 where
   range alone ran 38.5x: the transpose was built even after range had already
   eliminated nearly every pair. Prunes are not free and must be ordered by cost.
2. **Cascade** (range, then matrix only if >25% survive) fixed it — geomean
   8.21 vs 7.24 for the naive combo.
3. **Adaptive cascade** measures each stage's pruning power on the first tile and
   enables only stages that earn their cost — geomean 9.61.
4. **Within-tile sort by minimum element** makes range monotone, so the scan
   breaks at the first non-overlapping j. Improved absolute time on all six
   corpora. It also changed the *baseline* by up to 22x, which is why absolute
   ns/pair replaced "vs ilp8" as the reported metric from here on.
5. **Global sort FAILED, and instructively.** Sorting the whole corpus by
   minimum before tiling was expected to concentrate range pruning; it is
   geomean 0.89 and drops dimension_003 to 0.47x. Sorting by `lo` clusters
   *similar* extents into a tile, which maximises overlap and destroys exactly
   the pruning it was meant to create. The prediction was backwards.

#### The accounting error that dominated everything

Each variant above rebuilds a tile's transpose per visit and charges it to that
tile's 2,016 pairs. In real all-pairs, tile i pairs with every other tile, so its
transpose is built once and reused N/T times — 15,625 times at a million rows.
**The benchmark was overcharging the build by four orders of magnitude.**

With correct accounting (`amortised transpose`, absolute ns/pair):

| corpus | ilp8 | adaptive | **amortised** | amort speedup |
|---|---:|---:|---:|---:|
| dimension_003 | 56.68 | 0.91 | **0.13** | **436.0x** |
| com-LiveJournal | 42.60 | 7.29 | **1.70** | **25.0x** |
| soc-Pokec | 55.80 | 10.10 | **2.25** | **24.8x** |
| as-skitter | 42.82 | 6.62 | **2.14** | **20.0x** |
| com-Orkut | 149.13 | 37.06 | **22.93** | 6.5x |
| uscensus2000 | 6.02 | 5.00 | **1.09** | 5.5x |
| wiki-Talk | 69.18 | 31.70 | **22.53** | 3.1x |
| census-income | 2483.48 | 2597.71 | 2485.60 | 1.00x |
| weather_sept_85 | 4010.66 | 4452.77 | 4484.55 | 0.89x |

**C17: geomean 9.32x with the transpose amortised, against 3.66x charging it per
tile and 1.53x for the pair-level gated map of C16.** The C16 gate is retained
in front of the whole pipeline — without it the dense corpora regress to
0.39-0.42x exactly as before, because every prune here is built on the coarse
zone map and inherits its failure when it is not selective.

#### Limitations

- **T is hardwired to 64** by the 64-bit candidate words; larger tiles amortise
  the transpose better and are untested.
- The harness computes only **within-tile** pairs, the block diagonal of a real
  all-pairs run. Cross-tile blocks are where global ordering would matter, and
  are not measured.
- `weather_sept_85` sits at 0.89x under bypass where it should be 1.00x; that is
  noise, but it means worst case is not yet demonstrably par.
- Filter and transpose **build** costs are excluded from the amortised figure by
  construction — that is the point of the variant, but it makes the number an
  upper bound for workloads where each tile is visited once.
- One microarchitecture.

### 15.15 Wide tiles — and a dead-code measurement that briefly faked an 8x win

**Correction first.** The wide-tile variant initially reported 0.408 ns/pair on
com-LiveJournal (T=256) against 3.341 for T=64, an apparent 8x. It was measuring
an empty loop: the accumulator was stored to a local that nothing subsequently
read, so the compiler eliminated the entire pair loop. Adding a correctness
check against the same wide-tile pair set made the computation live and the
figure moved to 2.876 ns/pair. **Every number from that run is void.** The
earlier T=64 figures were unaffected -- `bench()` compares its sum to the oracle,
which keeps the work live -- but the wide path had no such check because it
bypassed `bench()`.

This is the second dead-store/stale-artifact class error in this project
(cf. the stale-binary reads in 15.2). The lesson is mechanical: **a timing
harness must consume its result, and every fast path needs its own oracle
comparison, not the one belonging to a sibling path.**

**Corrected result.** Build and resolve are O(T*|A|) while pairs grow as T^2/2,
so planning cost per pair should fall as 2|A|/T. Measured, T=256 against T=64
with the transpose amortised in both:

| corpus | T=64 amortised | T=256 | change |
|---|---:|---:|---:|
| as-skitter | 4.287 | **2.313** | 1.85x |
| dimension_003 | 0.178 | **0.104** | 1.71x |
| com-LiveJournal | 3.388 | **2.861** | 1.18x |
| com-Orkut | 31.300 | 33.095 | 0.95x |
| soc-Pokec | 2.739 | 3.861 | 0.71x |

Geomean 1.20x, but it **loses on 2 of 5**. The predicted 2|A|/T scaling does not
materialise cleanly: at T=256 the transposed tile is 4x larger and the candidate
rows are 4 words instead of 1, so the resolve's working set grows and offsets the
arithmetic saving. Tile width is therefore another gated decision, not a constant
to raise — consistent with C15's finding that cache residency, not operation
count, governs every structure in this stack.

All wide-tile results verified against a reference over the identical pair set.

**Status: T=256 is not adopted.** A 1.20x geomean that regresses on 40% of
corpora fails the genericity bar set in C16, and the mechanism to fix it (gate on
tile width) is not yet built.

### 15.16 Multi-occupancy resolve and bucket width — C15's rule reverses in the tile regime (C18)

Two further iterations on the tile pipeline, both verified against the oracle on
the identical pair set.

**Iteration 9 — resolve only multi-occupancy buckets. IMPROVEMENT.** The resolve
visited every occupied bucket, but a bucket held by exactly one row yields
`cand[i] |= (1<<i)` — itself, no pair. Only **0.08-5.6% of buckets hold two or
more rows**, so nearly all of that work produced no candidate. Keeping a compact
list of multi-occupancy words, built once with the transpose:

| corpus | amortised | multi-bucket | change |
|---|---:|---:|---:|
| dimension_003 | 0.132 | **0.039** | 3.38x |
| com-LiveJournal | 1.819 | **1.256** | 1.45x |
| soc-Pokec | 2.255 | **1.615** | 1.40x |
| as-skitter | 2.399 | **2.007** | 1.20x |
| uscensus2000 | 1.088 | **0.958** | 1.14x |
| com-Orkut | 24.586 | **23.010** | 1.07x |
| wiki-Talk | 24.738 | **23.820** | 1.04x |

**Iteration 10 — deduplicate the multi list. NO IMPROVEMENT.** `cand[i] |= w` is
idempotent so repeated occupancy words are exactly removable, but distinct words
are already 50-97% of the list and the sort costs what the skipping saves:
geomean 1.01x (range 0.95-1.11x). Not adopted.

**Iteration 11 — bucket width. LARGE IMPROVEMENT, and it reverses C15.**

| corpus | 16k | 65k | 262k | **1M** |
|---|---:|---:|---:|---:|
| uscensus2000 | 0.372 | 0.165 | 0.248 | **0.021** |
| dimension_003 | 0.040 | 0.027 | 0.052 | **0.018** |
| soc-Pokec | 1.873 | 0.654 | 0.242 | **0.156** |
| com-LiveJournal | 1.573 | 0.716 | 1.108 | **0.227** |
| as-skitter | 2.228 | 1.018 | 0.580 | **0.412** |
| com-Orkut | 20.658 | 9.994 | 4.923 | **2.870** |
| wiki-Talk | 27.385 | 19.352 | 14.504 | **10.731** |

1M bits (128 kB/row) wins on all seven, by 2.2x-17.7x over the 16k that C15
selected. Bracketed at 4M: worse for the graphs (soc-Pokec 0.393 vs 0.209) but
still improving for `uscensus2000` (0.014 vs 0.021), whose universe is 3.7e7 —
**the optimum scales with the universe, not with a constant.**

**C18: in the tile regime, wider is better; in the per-pair regime it is not.**
C15 found a fixed 2 kB beat wider filters because the probe had to hold the
filter in cache on every pair. Neither pressure survives here: the transpose is
built once and amortised across every block the tile joins, and the resolve
touches only multi-occupancy buckets — of which a wider filter produces *fewer*,
because collisions are what create them. Widening therefore cuts both the
resolve and the false-candidate rate at once. The same structure has opposite
tuning in the two regimes, which is a result about the access pattern rather
than about the data.

**Cumulative, from C17's amortised 16k baseline to multi-bucket at 1M:**
uscensus2000 51.8x, soc-Pokec 14.5x, com-Orkut 8.6x, com-LiveJournal 8.0x,
dimension_003 7.3x, as-skitter 5.8x, wiki-Talk 2.3x.

**Costs not charged, and they are not small.** At 1M buckets the per-tile
transpose is 8 MB and the corpus-wide filter set is 128 kB/row — for the 1,024-row
runs above that is ~128 MB of side structure against ~500 MB of bitmaps. The
build is excluded by the amortisation argument, which holds only when each tile
is paired with many others; for a single-block workload it would dominate
outright. Memory-constrained deployment would need a smaller width and would
land back near C15's answer.

### 15.17 Iterations 12-14 (C19)

**12 — bracket the width. No further improvement.** 4M buckets is worse than 1M
for the graphs (soc-Pokec 0.393 vs 0.209) and still improving for `uscensus2000`
(0.014 vs 0.021, universe 3.7e7). Optimum scales with universe; 1M is the graph
answer, not a universal constant.

**13 — empty-tile skip. NO IMPROVEMENT.** A tile with no multi-occupancy bucket
has every pair provably disjoint and can answer zero in O(1). It fires on
12.5-43.8% of tiles for the sparse corpora but geomean is 1.02x, and `wiki-Talk`
regresses to 0.79x where 0% of tiles are empty and the branch is pure cost.
Not adopted on its own.

**14 — direct pair emission. IMPROVEMENT.** At wide bucket widths the resolve is
nearly free and the fixed per-tile overhead dominates: clearing 64 candidate
words then scanning all 64 rows costs ~128 operations whether or not any pair
survives. Deriving the touched-row mask from the multi list and clearing and
scanning only those rows:

| corpus | multi-bucket | direct | change |
|---|---:|---:|---:|
| as-skitter | 1.302 | **0.406** | 3.21x |
| dimension_003 | 0.019 | **0.012** | 1.58x |
| uscensus2000 | 0.021 | **0.014** | 1.50x |
| soc-Pokec | 0.115 | **0.089** | 1.29x |
| wiki-Talk | 12.366 | **11.072** | 1.12x |
| com-LiveJournal | 0.217 | **0.208** | 1.04x |
| com-Orkut | 2.824 | **2.785** | 1.01x |

Geomean 1.42x, improves all seven, all verified against the oracle.

**C19: once the planning structure is wide enough, per-tile fixed overhead — not
the resolve — is the binding cost.** Both surviving optimisations at this stage
(multi-occupancy filtering, touched-row scanning) work by making per-tile cost
proportional to what is actually occupied rather than to T.

**Iteration counter for the standing goal:** improvements at 9, 11, 14; no
improvement at 10, 12, 13. Currently **0 consecutive** non-improvements, so the
loop is not finished. Untried angles remain: SIMD on the resolve, applying the
cascade to B x B and B x R rather than only B x S, gating tile width by universe,
and a two-level coarse-to-fine matrix.

### 15.18 Iteration 15 — a universe-derived width rule. NO IMPROVEMENT.

C18 showed the bucket-width optimum scales with universe, so a deployable
version needs a rule rather than a hardcoded 1M. Tested
`bits = clamp(next_pow2(universe/4), 65536, 4194304)` against the per-corpus
tuned optimum:

| corpus | universe | rule bits | rule ns | tuned ns | rule/tuned |
|---|---:|---:|---:|---:|---:|
| wiki-Talk | 2,394,385 | 1M | 10.111 | 10.302 | 1.02x |
| as-skitter | 1,696,415 | 512k | 0.460 | 0.415 | 0.90x |
| com-LiveJournal | 4,036,538 | 1M | 0.208 | 0.128 | 0.62x |
| com-Orkut | 3,072,627 | 1M | 2.767 | 1.669 | 0.60x |
| soc-Pokec | 1,632,804 | 512k | 0.160 | 0.067 | 0.42x |

The rule gives back 10-58% of the tuned gain, so universe alone does not
determine the optimum — cardinality and clustering must enter it. **Not
adopted**; width remains a tuned parameter, which means the tile pipeline is not
yet deployable in the C16 sense of working on unknown data.

(`uscensus2000` failed to produce a figure at 4M buckets in this run and is
excluded rather than reported.)

**Goal counter: 1 consecutive non-improvement.** Improvements at iterations 9,
11, 14; none at 10, 12, 13, 15.

### 15.19 Iterations 16-19 — four failures, loop terminates (C20)

**16 — prefetch surviving pairs. FAILED, 1.5-1.8x SLOWER.** Surviving pairs are
known before any data is touched, so the dense row's bitmap words can be
requested ahead of the probe. It loses everywhere (com-Orkut 5.043 vs 2.711,
wiki-Talk 16.914 vs 9.979): the prefetch pass walks the list a second time and
requests addresses the zone map then rejects. Prefetching in front of a filter
prefetches precisely the work the filter exists to avoid.

**17 — unrolled resolve (two occupancy words per iteration).** 0.98-1.04x.
Within noise; the ctz/clear-lowest dependency chain was not the bottleneck.

**18 — popcount-sorted multi list.** ~1.02x, mixed sign. No effect.

**19 — popcount-2 fast path.** Logically sound (a 2-row word needs only
`cand[i] |= w` for the lower row, since the higher row's candidates are masked
off anyway) and correctness-verified, but medians over 5 independent processes
give **1.009x, 1.010x, 1.084x, 1.010x** — geomean 1.03x, inside the documented
run-to-run band. Not a defensible improvement.

**C20: five consecutive iterations without improvement (15-19). The depth loop
terminates.**

Final configuration: C16 gate -> within-tile lo-sort -> adaptive cascade
(range, then tile transpose when enough pairs survive) -> multi-occupancy
resolve -> direct pair emission, at tuned bucket width.

| corpus | ilp8 ns | final ns | speedup |
|---|---:|---:|---:|
| dimension_003 | 56.68 | 0.010 | ~5,700x |
| soc-Pokec | 55.80 | 0.098 | ~570x |
| com-LiveJournal | 42.60 | 0.214 | ~200x |
| as-skitter | 42.82 | 0.408 | ~105x |
| com-Orkut | 149.13 | 2.571 | ~58x |
| wiki-Talk | 69.18 | 9.762 | ~7x |

**What the whole campaign found.** Across iterations 1-19 the improvements came
from removing work, never from executing it faster: the winners were the range
predicate (two comparisons), resolving all pairs at once by transposing the tile,
visiting only multi-occupancy buckets, and scaling per-tile cost to occupied
rows rather than to T. Every attempt at a faster *mechanism* -- hashing,
prefetch, unrolling, dedup, wide tiles, adaptive filter sizing, group
collapsing, probe-and-commit -- failed. That is the same result the pairing
matrix reported at pair level (C1: SIMD wins 0 of 25 asymmetric points), now
reproduced at tile level.

**The four caveats bounding every number above** are unchanged and important:
the transpose build is excluded by an amortisation argument that holds only for
genuine all-pairs; only within-tile pairs are measured; bucket width is still a
tuned parameter and iteration 15 could not derive it, so this is not yet
deployable on unknown data; and at 1M buckets the side structure is 128 kB/row.
One microarchitecture throughout.

### 15.20 Row hierarchies saturate; and the bypass path was using the wrong cell (C21)

#### Skip lists / dual-tree over rows — measured, and it is a dead end

Skip pointers over *elements* (Moffat & Zobel 1996) are already subsumed: the
zone map skips at O(1) per element, which O(log n) cannot beat, and the sparse
cells already gallop (`sr_search_runs`, `ss_adaptive2`).

The idea worth testing was a **dual-tree traversal over rows** (Gray & Moore):
recurse on node pairs, prune all |A|x|B| pairs when two nodes' union filters are
disjoint. The `group` angle was this at one level and showed alpha (13.45x on
uscensus2000). Measured at depth, with 1M buckets:

| node size | fill | node-pairs prunable |
|---:|---:|---:|
| 16 rows | 0.02-0.04% | **75-89%** |
| 64 rows | 0.08-0.14% | 14-41% |
| 256 rows | 0.3-0.6% | **0.0%** |
| 1024 rows | 1.2-2.2% | **0.0%** |

**C21a: union filters saturate quadratically and the hierarchy dies at ~256
rows.** Pruning needs two union sets disjoint; union size grows linearly in node
size k while collision probability follows a birthday argument,
P(disjoint) ~ exp(-n^2/NB) with n ~ 15k. At k=256 that is exp(-14.7) ~ 4e-7 --
exactly the measured 0.0%. A dual-tree therefore has at most two useful levels,
and the second prunes only 14-41%. **The existing 64-row tile already sits at the
saturation limit: the tile structure IS the hierarchy, and nothing can be built
above it.** Widening buckets pushes saturation out only as sqrt(NB) -- 4x deeper
nodes need 16x the memory, which is not a trade worth making.

Note also that the output-sensitive algorithm a hierarchy would provide is
already present: the transpose + multi-occupancy resolve iterates BUCKETS and
emits the pairs sharing one, at cost sum of popcount^2 over occupied buckets
rather than N^2. That is why only 0.08-5.6% of buckets do any work.

#### C21b: the bypass path was defaulting to a losing cell

When the gate rejects the zone map, the tile pipeline fell back to `B x S ilp8`.
The pairing matrix had already measured that B x S is the wrong cell on exactly
those corpora -- on weather_sept_85 and census-income, B x B runs 10.75x and
12.82x over tuned CRoaring while S x S runs 0.37x and 0.14x. Racing the
candidate cells on the tiles that bypass:

| corpus | B x S (was) | **B x B zonemap** | gain |
|---|---:|---:|---:|
| dimension_033 | 2522.1 | **47.8** | **52.8x** |
| census-income | 2382.8 | **323.9** | **7.4x** |
| weather_sept_85 | 4613.7 | **1115.0** | **4.1x** |

S x S and R x R are 6-25x *worse* than B x S there, confirming the density map:
above ~1e-2 density the dense cells win and every sparse representation loses.

This closes the one region where the all-pairs machinery contributed nothing.
The gate decides *whether* to filter; it must also decide *which cell* to run --
and the pairing matrix already contains that answer. The tile harness simply was
not consulting it.

**Correction to my own suggestion:** I proposed routing these corpora to the
galloping cells. That was wrong and the existing per-cell matrix already said so;
galloping loses badly at high density. The dense cells are the answer.
