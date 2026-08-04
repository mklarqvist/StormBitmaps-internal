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
| **0** | Foundation + representation layer + skewed data generators | 1.5 wk | builds, tests, CI green |
| **1** | Pairing-matrix skeleton + selection machinery (M1–M3) | 1 wk | **GATE 1 — P2** |
| **2** | Asymmetric cells vectorized: B×S, S×R (§4) | 2 wk | **GATE 2 — P3** |
| **3** | Rank index + B×R, B×W (M5, §4.5) | 1 wk | P4 measured |
| **4** | Cost model (M4) + density-sorted tiling | 1.5 wk | **GATE 3 — P1/P5** |
| **5** | B×B register blocking (§3) — the dense cell | 3–5 d | ceiling probed |
| **6** | Threading + triangle load balance | 1 wk | scaling measured |
| **7** | Cross-ISA + community | 2–3 wk (rolling) | ≥4 ISAs reported |
| **8** | Write-up | 2–3 wk | submission |

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

### Phase 1 — Selection machinery → **GATE 1 (P2)**

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
