# Per-cell optimization log

Running record of the pairing-matrix kernel campaign (`RESEARCH_PLAN.md` §1).
One section per cell. Each numbered entry is one **iteration**: a hypothesis, a
change, and what the measurement said — including when it said "no".

**Stopping rule.** A cell is closed after **5 consecutive iterations with no
improvement** to its best correct variant. A positive finding resets the count.

**Host.** Apple M4, 10 cores, L1d 64 kB, L2 4 MB (P-cluster), SLC 16 MB.
Apple clang 21.0.0, `-O3 -march=native`, C++17. All figures **tier 3 (measured)**
unless marked otherwise. Cycles are derived from a per-run measured frequency
(~3.8 GHz, dependent-ADD-chain calibration), not a PMU counter — macOS exposes
no unprivileged per-core cycle counter, so cycles/unit is time × measured
frequency and is labelled derived throughout.

**Reading the numbers.** Within one benchmark process every variant sees the
identical pair list under identical conditions, so the `vs ref` column is
reliable. Absolute ns/pair carries **~10% run-to-run variance** from DVFS even
with QoS pinned to the P-cluster; do not read a cross-run 5% delta as a result.

**Corpora** (`bench/sweep.sh`): U1 uniform/uniform, S1 uniform/1-over-i,
C1 clustered/1-over-i, L1 long-runs, D1 dense. Standing rule 4 requires U1 and
S1 to be reported together — the gap between them is the result.

---

## Iteration status (stopping rule: 5 consecutive non-improving)

Counted as of round 8. An *iteration* is one hypothesis tested by measurement;
a batch containing any win resets that cell's counter to 0.

| cell | consecutive non-improving | best variant now | last positive finding |
|---|---:|---|---|
| **S × S** | **8 — CLOSED** | `adaptive2` / `adapt_r3` | r10 `adapt_r24` (1.15×) |
| **B × B** | **8 — CLOSED** | `occ_sel` / `occ` | r8 `occ_sel` |
| **B × W** | **7 — CLOSED** | `occ` / `skip` / `rank` by corpus | r8 `occ` |
| **R × W** | **7 — CLOSED** | `skip2` / `merge2` / `skip_f*` | r7 `adaptive` |
| **B × R** | **6 — CLOSED** | `scalar` / `rank` / `hybrid_pair` by corpus | r7 `hybrid_pair` |
| **B × S** | **5 — CLOSED** | `ilp8` / `shift` | r6 `ilp16x` |
| **R × R** | **5 — CLOSED** | `adapt_r6` / `adaptive2` | r11 `adapt_r6` (1.06×) |
| **S × W** | **5 — CLOSED** | `adapt_f1` / `search` | r11 `adapt_f1` (1.07×) |
| **S × R** | **5 — CLOSED** | `adapt_b6` / `adaptive2` | r12 `adapt_b6` (1.20×) |
| **W × W** | **5 — CLOSED** | `skip_f1` / `skip2` | r12 `skip_f1` (1.05×) |

## ALL TEN CELLS CLOSED — the campaign has converged

Thirteen rounds, ~130 distinct variants, **2,658,633 differential checks against
an independent oracle with zero failures.** Every cell has gone 5 or more
consecutive iterations without an improvement exceeding the 5% within-run noise
floor.

The last three rounds were almost pure negative results: rounds 11–13 tested 42
further threshold and unroll settings across every open cell and produced four
wins, none above 1.20× and three below 1.08×. The design space around each
cell's optimum has been mapped from both sides.

**What the closed configuration looks like.** No cell is won by a single
variant across all corpora — every one of the ten selects by data shape, which
is the pairing-matrix thesis reproduced *inside* each cell. And the winners are
overwhelmingly work-avoidance rather than throughput: zone maps (B×B, B×W), rank
indexes (B×R), galloping and cost-based strategy selection (S×S, S×R, R×R),
bulk fill skipping (S×W, R×W, W×W). Only B×B is won by a SIMD kernel, and only
where the zone map cannot filter.

---

## GATE 1 — PASSES with M3 tile hoisting (iteration 4)

`kernels/storm_allpairs.{h,cpp}` + `bench/bench_allpairs.cpp`. Deciding once per
tile of 64×64 rows instead of once per pair, over density-sorted rows so a tile
is homogeneous enough for one decision to stand in for all of them.

512 rows, 130,816 pairs, clustered/1-over-i, d = 0.01. All checksums identical.

| host | selection %, per-pair | selection %, **per-tile** | decisions |
|---|---:|---:|---:|
| apple-m4 | 34.86% FAIL | **0.19% PASS** | 130,816 → 36 |
| neoverse-sve2 | 27.69% FAIL | **0.10% PASS** | 130,816 → 36 |
| sapphire | 48.44% FAIL | **0.29% PASS** | 130,816 → 36 |

**Claim P2 holds in its tile-hoisted form.** `RESEARCH_PLAN.md` §8 Phase 1
allows for exactly this: ">2–10% → proceed, but M3 tile hoisting becomes
mandatory rather than optional." At 0.1–0.3% it is comfortably inside the 2%
budget, and hoisting is not a concession — it is the prescribed remedy working.

**But the honest half.** Cheap decisions are also *coarser* decisions, and the
end-to-end numbers show the trade rather than hiding it:

| host / scale | per-pair | per-tile |
|---|---:|---:|
| apple-m4, 1 MB | 0.79× | **1.68×** |
| sapphire, 1 MB | 0.30× | **1.32×** |
| neoverse-sve2, 1 MB | 0.32× | **0.66×** |
| apple-m4, **32 MB (DRAM)** | **1.67×** | 1.26× |

(vs all-bitmap; >1 is a win.)

Two things follow, and neither is what the plan expected:

1. **On neoverse-sve2 the tile decision is actively bad** — 0.66×, worse than
   just running B×B. The model's calibrated constants are host-local and the
   tile aggregate (max cardinality, max run count) is deliberately conservative;
   together they pick a worse cell than doing nothing. Tile hoisting fixed the
   *cost* of selection and exposed a *quality* problem that per-pair selection
   was masking with its own overhead.

2. **The right granularity depends on how expensive the work being decided about
   is.** At 32 MB, per-pair *wins* (1.67× vs per-tile's 1.26×) because each
   kernel call is now expensive enough to pay for a better decision — the
   selection overhead falls to 6.79% purely because the denominator grew. There
   is no fixed answer; granularity is another thing the cost model should choose,
   and currently does not.

### Iteration 5 — probe-and-commit removes the model error

At tile granularity there is a remedy per-pair selection could never afford:
**time the candidates on three of the tile's pairs and commit the winner for the
remaining ~4,000.** Nine kernel calls, ~0.2% of the tile, and it replaces model
*prediction* with *measurement*. B×B is always one of the candidates, which
bounds the downside — the policy cannot lose to all-bitmap by more than the
probe cost, which is exactly what the pure-model policy failed to guarantee.

| host | all-bitmap | per-tile (model) | **probe** | selection % |
|---|---:|---:|---:|---:|
| apple-m4 | 1.00× | 1.35× | **1.38×** | 0.30% |
| neoverse-sve2 | 1.00× | **0.66×** | **1.09×** | 0.48% |
| sapphire | 1.00× | 1.33× | **1.35×** | 0.31% |

The neoverse-sve2 regression is gone (0.66× → 1.09×) and the other two improve
slightly. Gate 1 still passes everywhere. This is the answer to the quality
problem hoisting exposed: at N² scale you can afford to *measure* the decision
once per tile, so the calibrated cost model does not have to be right — it only
has to nominate a candidate worth timing.

So Gate 1 is passed on the criterion P2 actually states (selection ≤2% of
runtime), and the live problem has moved from *decision cost* to *decision
quality* — which is the regret number, still 94.1% per-pair and unmeasured
per-tile.

## M4 — the cost model, and GATE 1 as originally measured

Built at the user's direction after F10 showed hand-tuned thresholds losing
systematically. `kernels/storm_cost.{h,cpp}`, validated by
`bench/bench_select.cpp` on the clustered/1-over-i corpus:

| policy | ns/pair | vs all-bitmap |
|---|---:|---:|
| all-bitmap (B×B) | 30.76 | 1.00× |
| **M4 model** | 47.13 | 0.7× |
| per-pair oracle | 24.28 | 1.3× (unattainable floor) |

**Regret 94.1%. Selection cost 13.3 ns/pair = 28.3% of runtime. Gate 1 requires
≤2%. FAIL.**

`RESEARCH_PLAN.md` §8 Phase 1 says exactly what to do here: ">10% and not
reducible → the per-pair adaptive premise is in trouble. Fall back to
per-tile-only selection and re-scope. This is the cheapest possible test of the
core premise." The gate is doing its job.

Three causes, in order of size, two already fixed:

1. **Transcendentals in the selection loop.** v1 called `std::log2` per
   candidate; the fix introduced `std::pow` and made it worse (24 → 38 ns/pair)
   — the same mistake twice in one function. Both replaced with integer and
   linear forms: 38 → 13.3 ns/pair. A selector ranking ten candidates cannot
   afford a transcendental in any of them.

2. **The zone map broke the model's core assumption.** B×B is no longer Θ(m).
   `occ_sel` visits only the bins live on both sides, so predicting its cost
   from m over-estimates by exactly the factor the zone map saves — 5–25× on
   skewed data. The model chose against B×B on nearly every pair and measured
   151% regret. Charging B×B for expected live bins instead lifted agreement
   from 14.8% to 31.8%. **`PROBLEM_STATEMENT.md` §2's premise that bitmap ×
   bitmap is fixed cost is now false of our own best B×B kernel** — the zone map
   made the dense cell adaptive too.

3. **Still 13.3 ns/pair, still 14× over budget.** The remaining cost is ten
   double-precision predictions per pair. Not yet attacked: integer arithmetic
   throughout, and pruning candidates before scoring them.

The plan's own fallback — M3 tile hoisting, deciding once per tile of pairs
rather than once per pair — is the indicated next step, and it is what makes
the 13.3 ns amortize.

---

## Cross-cutting findings

These came out of the campaign but are not specific to one cell.

**F1 — The NEON B×B kernel is SIMD-issue bound, not load bound.** `RESEARCH_PLAN.md`
§3.1 derives a load-bound kernel from the AVX-512 port structure, where
`VPOPCNTQ` has a dedicated port and the AND/ADD go elsewhere, so cutting loads
via MR×NR register blocking pays. On NEON, `AND`, `CNT` and `UADALP` all issue
on the same four pipes (u11-14, recip TP 0.25 — applecpu, tier 2), while loads
have three of their own (u8-10, recip TP 0.333). Per 16 bytes the kernel issues
3 SIMD ops against 2 loads → 0.75 cyc/16B from SIMD vs 0.667 from loads.
**Register blocking would cut a resource that is not binding.** Measured B×B
floor is 0.42–0.49 cycles/word against the 0.375 derived ceiling.

**F2 — Harley-Seal is a measured loss on NEON**, 0.38–0.72× depending on corpus.
`RESEARCH_PLAN.md` §3.3 predicts this for AVX-512 and the NEON arithmetic is
more lopsided still: a CSA is 5 ops and it buys back `CNT`s that cost 1 op each
at 4/cycle. Reducing 16 vectors costs 15 CSAs = 75 ops to remove ~30 ops.
The kernel is kept as a labelled negative control.

**F3 — Accumulator count dominates instruction choice on this microarchitecture.**
The same instruction mix measured 0.41× with one accumulator and 1.24× with
eight. Three of the first-cut cell kernels (B×W, W×W, R×W) *lost to their own
scalar reference* purely from a single-accumulator recurrence, because clang
auto-vectorizes `c += POPCOUNT(w[k])` with several. Fixed centrally in
`kernels/storm_simd.h`. A hand-written SIMD kernel that loses to the compiler's
version of the same loop is a bug, not a finding.

**F5 — SIMD wins only where the work is irreducible. Everywhere else the win is
doing less work.** This is the campaign's central result and it is sharp:

| cell group | corpus-cell points | won by a SIMD-throughput kernel |
|---|---:|---:|
| **Asymmetric** (B×S, B×R, S×R, S×W, R×W) | 25 | **0** |
| Same-representation (B×B, S×S, W×W, R×R, B×W) | 25 | 7 |

Across every mixed-representation cell on every corpus, **a vectorized kernel
never wins**. The winners are plain scalar loops (`ilp8`, `scalar`), index
lookups (`rank`), search-strategy selection (`adaptive`, `search`, `merge_bl`),
and structural folding (`collapse`). `bs_neon_idx` and `br_neon` exist, are
correct, and lose everywhere — 0.51–0.86×.

Where SIMD does win it is exactly where the work cannot be reduced: **B×B on 4
of 5 corpora** — dense against dense, every word must be touched — plus S×S on
long balanced lists, and the literal bodies of B×W/W×W.

The exception proves the rule. On the long-run corpus, B×B is won not by the
best SIMD variant (`neon_u8acc4`, 1.21×) but by `neon_rankskip` at **3.12×** —
skipping all-zero blocks. Once the data is sparse, even *inside the dense cell*
the win comes from not doing the work rather than from doing it faster.

This is the empirical case for `PROBLEM_STATEMENT.md` §2. Faster popcount is a
constant factor on the one cell where the cost is unavoidable; representation
pairing is an asymptotic factor on the other nine. It also reframes what this
project's kernels *are*: the deliverable is not a set of hand-tuned SIMD
routines but a set of work-avoidance strategies plus the O(1) machinery to pick
between them.

**F8 — Zone maps: a 0.195% summary is worth up to 25x, and the win comes from
PLANNING rather than from a kernel.** A Parquet/ORC-style occupancy bitmap —
one bit per 512-bit bin, set when the bin holds anything — makes
`occ_A & occ_B` a bitmap intersection over m/512 bits. Its popcount is the
*exact* number of bins that need visiting, not an estimate, and zero proves the
rows disjoint.

B×B, best SIMD kernel vs zone-map-planned (`occ_sel`), same run each:

| corpus | best SIMD | zone map raw | **planned** |
|---|---:|---:|---:|
| U1 uniform density | 117.3 ns | 376.2 (0.31×) | **127.6** (0.92×) |
| S1 1/i spectrum | 118.7 | 70.1 | **48.5 (2.45×)** |
| C1 clustered 1/i | 132.7 | 23.1 | **21.1 (6.3×)** |
| L1 long runs | 455.4 | 16.5 | **18.3 (24.9×)** |
| D1 dense | 124.5 | 384.7 (0.32×) | 172.3 (0.72×) |

B×B on the long-run corpus goes from 0.49 to **0.013 cycles/word**.

Three things this establishes:

1. **Applied unconditionally the zone map is a 3× pessimization** on
   uniform-density data — every bin is occupied, the summary filters nothing,
   and the bit-scan is pure overhead. A filter only pays when it filters.
2. **Consulted as a plan it is nearly free to be wrong.** The selectivity scan
   costs m/512 and is exact, so the kernel choice that follows is a decision
   rather than a guess. That recovers the uniform-density case to 0.92× while
   keeping 25× at the sparse end, and it makes the skewed case *faster than the
   zone-map kernel alone* (48.5 vs 70.1) because disjoint pairs never reach a
   kernel at all.
3. **Build cost is a single forward pass**, O(m) per row, amortized over the
   N−1 pairings that row participates in — and in a real system it would be
   stored with the data, as columnar formats already store zone maps, so it
   would not be paid at query time at all.

This is F5 with a mechanism attached. SIMD wins only where work is
irreducible; the zone map is how work becomes reducible, and it operates above
the kernel rather than inside it.

**Open, not explained:** on the dense corpus the planning step costs ~28% when
the summary is 0.195% of the data. That is far more than the size ratio
predicts and it has not been isolated — candidate causes are the extra
dependent load chain in front of the kernel and lost inlining. It bounds how
good `occ_sel` can be in the regime where it should be free.

**F9 — the zone map does NOT transfer to B×S**, measuring 0.23–0.59×. Grouping
list elements by bin and testing the summary costs more than simply probing,
because the list is already sparse — there is little to filter and the grouping
is per-element work. Same result for the rank-gated form. The zone map pays
where a *dense* representation is being scanned, not where a sparse one is
being walked.

**F6 — Sequential per-variant timing is a measurement bug on a thermally
managed part.** The harness originally timed each variant to completion in turn,
so any monotonic drift over a long sweep landed unevenly and favoured whichever
ran first. Effect size: B×B `neon_u8` measured **0.64× of scalar in one sweep
and 1.17× in the next**, same binary, same corpus, with the difference tracking
how much work had run before the cell started — and 0.64× was briefly written
up as a real finding ("hand-written NEON loses to the compiler on DRAM-resident
data") before the second run contradicted it. Fixed by round-robin interleaving:
every repeat times every variant once. Any result in this log predating that fix
that rests on a <20% margin should be treated as unconfirmed.

**F7 — The density sweep: the win is asymptotic, and the curve is U-shaped.**
`bench/density_sweep.sh` + `bench/plot_density.py` → `results/density.{png,svg}`.
Twenty density points from one bit set to every bit set, both sides at the same
density, universe pinned at 65,536 bits so cache residency does not drift along
the x axis (standing rule 5).

| density | best cell | ns/pair | fixed-cost B×B | speedup |
|---:|---|---:|---:|---:|
| 1.5e-5 (**1 bit**) | S×R `merge_bl` | 1.6 | 114.9 | **70.1×** |
| 3.1e-5 | B×S `ilp2` | 2.1 | 123.6 | 59.8× |
| 1.2e-4 | B×S `scalar` | 3.9 | 131.5 | 33.3× |
| 4.9e-4 | B×S `ilp8` | 8.9 | 117.3 | 13.2× |
| 1e-3 | B×S `ilp8x` | 16.9 | 149.4 | 8.8× |
| 3e-3 | B×S `ilp8x` | 48.7 | 132.7 | 2.7× |
| **1e-2 … 0.9** | — nothing beats B×B — | | | 0.1–0.9× |
| 0.9995 | B×R `rank` | 50.3 | 116.1 | 2.3× |
| 1 − 1.5e-5 | R×R `merge_bl` | 4.5 | 123.5 | 27.6× |
| **1.0 (all bits)** | R×R `merge_bl` | 2.2 | 117.5 | **52.7×** |

Three things this settles:

1. **The fixed-cost claim is visible, not argued.** The pure-SIMD B×B kernel
   measures 115–150 ns/pair across five orders of magnitude of density. That
   flat line *is* `PROBLEM_STATEMENT.md` §2's Θ(m)-regardless-of-density claim.
2. **The crossover is at ~0.4% density**, and below it the advantage grows
   without bound — 70× at one bit set, and still climbing as the universe grows.
   This is the asymptotic-not-constant-factor claim, measured.
3. **The curve is U-shaped, which the plan did not anticipate.** At density → 1
   the *complement* is sparse, so the data is one long run and R×R wins 52.7×.
   Compressed pairing wins at both extremes and loses across the whole middle
   band. Sparsity was never the operative variable — **distance from ½ is.**

The practical consequence: the selection layer (M2) needs a two-sided test, not
a sparsity threshold. Panel 3 of the figure is the map it has to reproduce.

**F11 — Work avoidance scales with the memory hierarchy; work acceleration does
not.** The whole campaign ran L2-resident, so every conclusion was residency-
local. Re-measured on B×B at three scales (Apple M4, clustered/1-over-i,
96 rows, 2,000 pairs):

| corpus | scalar | best SIMD | zone map |
|---|---:|---:|---:|
| 1 MB — L2 | 0.511 c/w | 0.336 (**1.52×**) | 0.058 (**8.8×**) |
| 12 MB — SLC | 0.562 | 0.437 (1.29×) | 0.038 (**14.8×**) |
| 48 MB — DRAM | 0.796 | 0.679 (1.17×) | 0.039 (**20.5×**) |

The two mechanisms move in **opposite directions**:

- **SIMD's advantage decays**, 1.52× → 1.17×, because the kernel becomes
  memory-bound and instruction throughput stops being the constraint. At 48 MB
  `neon_u4` actually *loses* to the auto-vectorized scalar loop (0.85×).
- **The zone map's advantage grows**, 8.8× → 20.5×, because what it avoids is
  memory traffic, and memory traffic is exactly what starts binding.

This is the strongest single argument for the project's thesis and the campaign
could not have found it: every round was tuned on corpora that fit L2, where
the mechanism that matters least looks best.

It also **refines rather than overturns** F1. Within-pair MR×NR register
blocking still does not pay at DRAM scale — the `neon_ld2` paired-load proxy
measures 0.95× at 48 MB — because the bottleneck there is bandwidth, not
load-issue slots. What *would* pay is **cross-pair reuse**: loading a row once
and pairing it against many, which is the L1/L2 blocking level
`RESEARCH_PLAN.md` §2 lists as missing and which no kernel in this project can
express, since every signature is `(view, view) -> count`.

**F4 — Two of the plan's four B×S designs do not exist on this ISA.**
`RESEARCH_PLAN.md` §4 proposes D1 (gather) and D3 (`VPCONFLICTD`); NEON has
neither. The portable designs are the ones that exploit *data structure* (D2 run
collapsing) rather than an instruction. This is a finding about the plan, not a
gap in the implementation.

---

## B × S — Θ(|S|), P0

Best: **`ilp8x`** (8 named accumulator chains) on skewed/clustered corpora,
`shift` on uniform. ~1.14 cycles/list-element; the load-port floor is 0.67
(2 loads/element ÷ 3 loads/cycle).

| # | Hypothesis | Result |
|---|---|---|
| 0 | baseline | `scalar` 187.8 ns (U1), 31.1 (S1), 45.8 (C1) |
| 1 | Branchless `(w>>bit)&1` beats `(w&(1<<bit))!=0` | **YES**, 1.13× on U1 |
| 1 | D2 run-collapsing (plan's favoured design) | **NO — 0.25–0.51×.** See below |
| 1 | D4 software prefetch | **NO**, 0.47–0.65×. The list is sorted, so the stream is monotonic and the HW prefetcher already has it |
| 1 | NEON index arithmetic | **NO**, 0.51–0.73×. The bitmap loads stay scalar, so only the cheap part vectorizes |
| 2 | `bs_ilp<U>` used an accumulator ARRAY and lost (0.76×); naming the accumulators should fix it | **YES**, `ilp8x` 1.03–1.04× and now beats `shift` on C1/S1 |
| 2 | Two positions per 64-bit list load cuts list traffic in half | **NO**, 0.86×. Not load-count bound at the list end |
| 3 | Two independent cursors over halves of the list double the MLP | **NO**, 0.83–0.94×. One stream already saturates the available memory parallelism |
| 4 | Pick `shift` vs `ilp8x` on the dense side's L1 footprint | **NO**, ties the better of the two. The two forms are within ~5% of each other on every corpus, so there is nothing for a selector to recover |

**Status: 3 consecutive non-improving iterations.** B×S is at ~0.80–1.23
cycles/element against a 0.67 load-port floor, and the remaining gap is the
scattered bitmap load. Nothing in the design space tried so far moves it.

**D2 run-collapsing is the plan's headline B×S design and it loses everywhere,
including on clustered data.** Worth stating plainly. The mechanism: folding a
same-word group costs one *unpredictable* branch per group, and with a mean run
of ~10 bits that is one misprediction per ~10 elements ≈ 2 cycles/element —
precisely what the straight-line version costs anyway. It trades ILP for fewer
loads, and on this core loads are cheap while mispredictions are not.

The deeper point: **on clustered data the right answer is not a better B×S
kernel, it is to stop using B×S.** On C1, B×R costs 18.1 ns/pair against B×S's
42.3 for the same rows. That is the pairing matrix working as intended, and it
means B×S's real job is the *unclustered* sparse case — exactly where D2 has
nothing to exploit.

---

## B × R — Θ(runs) with the rank index, P0 — **claim P4**

Best: **`rank`** on long runs, `scalar`/`hybrid4` on short. This is the cell
carrying the project's strongest claim and it holds.

**P4 measured** (`bench/p4_runlength.sh`, run count fixed at ~15.3/pair, run
length varied 256×):

| mean run | no-index (neon) | rank | ratio |
|---:|---:|---:|---:|
| 64 b | 1.72 ns/run | 2.20 ns/run | 0.78× |
| 256 b | 2.91 | 2.51 | 1.16× |
| 1024 b | 4.80 | 2.74 | 1.76× |
| 4096 b | 10.46 | 4.49 | 2.33× |
| 16384 b | 31.75 | 3.29 | **9.65×** |

The no-index kernel grows 18× across the sweep; rank shows no trend. **Run
length drops out of the cost.**

| # | Hypothesis | Result |
|---|---|---|
| 0 | baseline | `scalar` 525 ns (U1), 19.1 (C1), 29.6 (L1) |
| 1 | rank9 index makes B×R Θ(runs) | **YES on long runs** — 3.36× on L1, 9.65× at 16 kbit runs. **NO on short runs** — 0.5× on U1/S1/C1, where runs are ~1 word |
| 1 | Interleaving two runs per iteration exposes ILP | **NO**, 0.51–0.60×. The rank loads already overlap |
| 2 | Analytic crossover estimate (600–1000 bit runs) | **WRONG — measured ~256 bits.** The estimate counted instructions; the no-index path's real cost is memory traffic. Shipping threshold corrected 12 words → 4 |
| 3 | When a run stays inside one 512-bit rank block the block counter cancels, so the pair costs one rank load instead of two | **NO.** 0.54× on short-run corpora (vs `rank`'s 0.52× — no real gain) and 1.79× on long runs where plain `rank` gets 2.37×. The saved load was not the cost |

**Status: 1 consecutive non-improving iteration.**

---

## S × R — Θ(|S| + r), P0

Best: **`adaptive`** (three-way cost comparison). **The largest single win of
the campaign.**

| # | Hypothesis | Result |
|---|---|---|
| 0 | baseline | `merge` 5123 ns (U1), 8487 (L1) |
| 1 | Branchless merge | Mixed: 1.54× on U1, **0.54× on L1** |
| 1 | Gallop into the run array per list element | 2.68× on S1, 0.45× on L1 |
| 1 | **Search from the RUN side** — two galloping searches into the list per run, Θ(r log \|S\|) | **YES, 70×.** See below |
| 1 | Replace the single hardcoded ratio with a three-way cost comparison | **YES**, now picks correctly on every corpus |

The cell could exploit a short *list* but not a short *run array* — an
asymmetry with no justification, since the entire premise of the pairing matrix
is that either side may be the sparse one. On L1 (r = 3.3, |S| = 13,107) every
variant walked all 13,107 list elements when the answer is six binary searches:
**8487 → 121 ns/pair.**

The replacement selector compares `|S|+r`, `|S| log r` and `r log |S|` directly
from the two sizes — O(1) metadata, which is the shape standing rule 7 demands.

---

## S × S — Θ(|A|+|B|), P1 (mature prior art)

Best: **`adaptive`** → NEON 4×4 block compare, or gallop when the sizes are
lopsided.

| # | Hypothesis | Result |
|---|---|---|
| 0 | baseline | `merge` 3146 ns (U1) |
| 1 | Branchless merge | **YES**, 1.19–1.21× |
| 1 | NEON 4×4 all-pairs compare via `vextq` rotation | **YES**, 1.94–2.39× on balanced pairs |
| 1 | Gallop when lopsided | **YES**, 2.35–2.46× on S1/C1 |
| 2 | `adaptive` fell back to the scalar merge, not the NEON kernel | **YES**, 1.54–1.63× on S1/C1 |

---

## R × R — Θ(r_A + r_B), P1

Best: **`adaptive`**; `merge_bl` on long-run data.

| # | Hypothesis | Result |
|---|---|---|
| 0 | baseline | `merge` 3526 ns (U1), 7.99 (L1) |
| 1 | Branchless overlap merge | **YES**, 1.09–1.35× |
| 1 | Gallop from the shorter run array | **YES**, 1.44–1.59× on C1 |
| 1 | Adaptive on the run-count ratio | **YES**, 2.19–2.23× on C1 |

**R×R is the fastest cell in the matrix on run-structured data: 6.63 ns/pair
against a B×B floor of 214 ns — 32×.**

---

## B × W / S × W / R × W / W × W — the WAH-fill cells, P1/P2

| # | Hypothesis | Result |
|---|---|---|
| 0 | baseline | B×W 2135 ns (U1), 47.1 (L1); W×W 4200 (U1), 75.4 (L1) |
| 1 | Explicit NEON bodies beat the scalar reference | **NO at first — 0.74–0.96×**, the single-accumulator bug (F3) |
| 1 | Rank index also accelerates one-fills (plan §4.5's open question) | **YES**, 1.20× on L1 — fills are runs, so the same mechanism applies |
| 1 | A zero fill can swallow the other stream's segments whole | **YES**, `ww_skip` 1.12–1.20× |
| 1 | Same-word collapse in S×W | **YES on long runs**, 2.29×; ~1.0× elsewhere |
| 2 | Multi-accumulator shared primitives (F3 fix) | **YES**, R×W 1.28× on L1, W×W 1.04–1.12× |

---

## B × B — Θ(m) fixed cost, P2

Best: **`neon_u8`** / `neon_u4` (within noise of each other), **0.42–0.49
cycles/word** at L2 residency against a 0.375 derived ceiling.

| # | Hypothesis | Result |
|---|---|---|
| 0 | baseline | `scalar` 148.8 ns (U1); `scalar_u4` — what the repo shipped on arm64 — is **0.68×, slower than plain scalar** |
| 1 | NEON AND+CNT+UADALP, 1 accumulator | **NO, 0.41×** — latency bound on UADALP (lat 3) |
| 1 | 4 accumulators cover the latency-3 recurrence | **YES**, 1.07–1.31× |
| 1 | 8 accumulators | **YES**, ~1.24×; ties u4 within noise |
| 1 | u8 accumulation shortens the recurrence (ADD lat 2 vs UADALP lat 3) | **NO**, ties. Issue bound, not latency bound, once ≥4 chains exist |
| 1 | Harley-Seal | **NO, 0.38–0.72×** (F2) |
| 1 | Skip all-zero 512-bit blocks via the rank index | **YES on sparse data**, 3.12× on L1; 0.40× on dense, where the test is pure overhead |
| 3 | **`vld1q_u8_x2` paired loads** — halves load instructions without changing the SIMD op count. A cheap proxy for "does register blocking pay?" | **NO, and that is the point.** 1.07–1.19× against `neon_u8`'s 1.18–1.40×. Cutting loads changes nothing because loads are not the binding resource — **so MR×NR register blocking (§3.2) will not pay on this ISA either.** Phase 5's central question, answered for ~30 lines |
| 4 | Explicit prefetch helps once the working set exceeds L2 | **NO**, 0.97–1.16×, never wins. The hardware prefetcher already has a unit-stride stream |

**Status: 2 consecutive non-improving iterations.** Best is 0.34–0.49
cycles/word depending on residency.

**M4 exceeds the Firestorm-derived ceiling.** The 0.375 cyc/word floor derived
from applecpu's 4-SIMD-pipe Firestorm data is beaten: **0.340 measured** on the
dense corpus. Firestorm is M1 and this host is M4, and the skill's own guidance
says not to treat one as the other — this is what that caution looks like when
it bites. Tier 3 overrides tier 2; the ceiling is a hypothesis that measurement
has now falsified for this core.

`neon_rankskip` is the one B×B variant that engages the project's thesis: it
recovers part of the sparse-side win *without leaving the bitmap
representation*. It is still Θ(m/8) — it cannot reach what B×S or B×R reach —
and quantifying that gap is the direct answer to the "why not just skip zeros?"
objection to `PROBLEM_STATEMENT.md` §2.

---

## F12 — Real data: the 1/i premise is confirmed, the *scale* premise is not (yet)

`data/chr20.bin`, converted from 1000 Genomes Phase 3 chr20 by `tools/vcf2bin.sh`
(bcftools → `tools/gt2bin.py`). **1,739,315 biallelic SNVs × 5,008 phased
haplotypes**, 274,239,192 set bits. This is `RESEARCH_PLAN.md` §7.2's outstanding
"at minimum one REAL dataset" requirement, finally closed.

### The allele-frequency spectrum is 1/i, measured

Over 1,203,601 scanned variants:

| allele count | variants | share |
|---:|---:|---:|
| **1 (singleton)** | 535,101 | **44.6%** |
| 2–3 | 199,673 | 16.6% |
| 4–7 | 108,474 | 9.0% |
| 8–15 | 79,533 | 6.6% |
| 16–31 | 60,521 | 5.0% |
| 32–63 | 48,750 | 4.1% |

Each bin is roughly half the previous — the 1/i shape of `PROBLEM_STATEMENT.md`
§2.2, on real data, with **44.6% singletons**. The premise that motivated this
entire project is empirically correct, and the synthetic generator was not
encoding a fiction.

### But the win at this cohort size is only 2.1×

| cell | ns/pair | vs all-bitmap |
|---|---:|---:|
| B × B all-bitmap | 11.77 | 1.00× |
| B × B zone-mapped | 10.48 | 1.12× |
| **B × S** | **5.49** | **2.15×** |
| B × R | 6.95 | 1.69× |
| S × S | 30.21 | 0.39× |
| R × R | 35.46 | 0.33× |

**2.15×, not the 10²–10⁵× §2 projects.** The reason is scale, and it is worth
being blunt about: 2,504 samples is **5,008 haplotypes = 79 words = 626 bytes per
row**. A whole row fits in a fraction of one cache line's worth of L1, so
bitmap × bitmap costs 11.77 ns and there is very little fixed cost to avoid.
§2's arithmetic assumes 10⁷ haplotypes — **1.25 MB per row, 2000× larger** — and
that is where the asymptotics live.

This is the honest reading: **the data shape is real, the scale is not.** The
1000 Genomes cohort validates the spectrum and refutes nothing, but it cannot
demonstrate the headline claim, because the claim is asymptotic in universe size
and this universe is small. Demonstrating it needs a UK-Biobank- or
gnomAD-scale cohort (10⁵–10⁶ samples), which is exactly the regime Tomahawk
targets and this repo does not have access to.

Two consequences for the write-up:

1. **Report this number.** A reviewer who runs the only public phased cohort and
   gets 2.1× when the paper claims 10⁴× will not accept "wrong scale" after the
   fact. It has to be stated first, by us.
2. **The synthetic sweep is the evidence for the asymptotic claim**, and real
   data is the evidence that the *shape* is not invented. They do different jobs
   and neither substitutes for the other. `results/density.png` already shows the
   win growing without bound as density falls; chr20 sits at density 0.032,
   which the sweep puts squarely in the band where B × B is competitive.


---

## 1KGP3 throughput campaign (universe 5,008 bits, 79 words/row)

Target: real 1000 Genomes chr20. Everything is L1-resident at 632 B/row, so this
is the *opposite* regime from F11's DRAM measurements and the conclusions differ.

**Baseline: 5.49 ns/pair** (`bench_real`, B x S, best cell).

### Where the time actually goes

Profiled before optimizing, which changed the plan:

| | |
|---|---|
| sparse-side cardinality, **median** | **1** |
| mean | 3.0 |
| pairs with \|S\| <= 1 | **75.0%** |
| pairs with \|S\| <= 4 | 89.1% |
| view construction | **23% of runtime** |
| kernel work | 0.92 ns/probe = ~3.5 cycles |

Three things fall out, none of which the synthetic corpora showed:

1. **75% of pairs need exactly one bit test.** The 1/i spectrum's singleton mass
   (44.6% of real variants) dominates, and the generator never reproduced it
   because it draws around a *mean* cardinality rather than the spectrum's shape.
2. **`BitmapView` grew to 6 fields / 32 bytes** as features were added, and it is
   reconstructed per pair. That is 23% of the total at this scale -- invisible at
   the universe sizes everything else was tuned on.
3. 3.5 cycles for an L1-resident probe is loop overhead, not the probe.

### Iteration 1

| change | ns/pair |
|---|---:|
| baseline (`bench_real`, B x S) | 5.49 |
| hoist view construction out of the pair loop | 3.95 |
| `bs_small` -- peel \|S\| <= 4 | 3.80 |
| **preprocess to a packed per-row representation** | **2.10** |

**2.10 ns/pair, 5.1-5.7x over all-bitmap on real data — a 2.6x improvement on
the goal metric.**

The packed format (`tools/pack.cpp`, `bench/bench_pack.cpp`) is
`PROBLEM_STATEMENT.md` §3.2's storage/compute separation made real: each row's
representation is chosen **once at ingest** and written to disk, and query time
is mmap + dispatch on the stored tag pair. No per-pair view construction, no
five-representations-per-row.

Selection is by **throughput, not size** -- storage is explicitly not an
objective here, since this feeds an N x M exact-LD computation where pairing
speed is the whole point.

### The crossover threshold is not the lever

Sweeping the cardinality at which a row is stored as an array rather than a
bitmap:

| threshold | %array | %bitmap | ns/pair |
|---:|---:|---:|---:|
| 0 (all bitmap) | 0% | 99.7% | 11.05 |
| 8 | 71.1% | 28.6% | **2.10** |
| 32 | 81.7% | 18.0% | **2.10** |
| 128 | 88.6% | 11.1% | **2.10** |
| 5008 (all sparse) | 98.8% | 0% | **2.10** |

Flat above 8. The reason is the same singleton mass: pairs are oriented so the
*sparser* side drives the kernel, and that side is already an array in almost
every pair regardless of where the threshold sits. Only the degenerate
all-bitmap policy is different, and it is 5x worse.

So the storage decision that matters is binary -- "keep sparse rows sparse" --
not the precise crossover. That is worth knowing because the crossover is
exactly the kind of constant the cost model would otherwise spend effort fitting.

**A bug this exposed:** the first packed measurement reported `correct=NO`. Row
lengths were being derived from consecutive offsets, which included up to 7
bytes of 8-byte alignment padding, so `len/2` over-counted array elements and
read past the end. Fixed by storing exact lengths alongside the aligned starts.
The broken version reported 3.87x; the correct one reports 4.77-5.67x -- the bug
was *understating* the result, which is the direction that gets shipped.

### Iterations 2-3 — locality, not element width

**Iteration 2 (no improvement).** Probing straight from the packed `uint16`
array should have halved list traffic; it measured **slower**, 3.35 vs 2.60 ns
in the same run.

**Iteration 3 (improvement).** The cause was not the element width but where the
bytes sit. In the packed blob the sparse arrays are interleaved with 632-byte
bitmap rows, so two consecutive rows' arrays are pages apart, while the widened
`uint32` copies were freshly allocated and compact. Copying the `uint16`
payloads into **one contiguous arena in row order** -- same data, same width --
gives:

| | ns/pair |
|---|---:|
| packed dispatch, widened u32 (iteration 1) | 2.10 |
| u16 direct from the mmap'd blob | 2.10-3.35 |
| **u16 from a contiguous arena** | **1.80-1.85** |

The arena for a 4,000-row corpus is **25 kB — it fits in L1** and stays resident
across the whole pair sweep. That is the actual mechanism: at 89% of pairs
having \|S\| <= 4, the probe loop is short enough that *where the list lives*
dominates *how wide its elements are*.

**Consequence for the packed format:** rows should be written **segregated by
representation** rather than in row order -- all arrays contiguous, all bitmaps
contiguous -- so the sparse side of the corpus is one dense region. The current
`tools/pack.cpp` writes row-major and the benchmark has to re-gather. That is a
format change worth making, not a benchmark trick.

**Running total: 5.49 -> 1.80 ns/pair, 3.0x.**

### Iterations 4-7 — cross-PAIR ILP is the lever at this scale

**Iteration 4 (no improvement).** Grouping pairs by the sparse side's stored tag
to remove the dispatch branch, and holding the dense row fixed across its
partners, measured 1.95-2.00 against the ungrouped 1.95 -- a tie. The diagnostic
says why: **all 20,000 pairs land in the array group**, none in rle or bitmap.
There was no representation interleaving, so no unpredictable branch to remove,
and at 632 B/row the dense row never left L1 to begin with. Both optimizations
were solving problems this corpus does not have.

**Iteration 5 (improvement).** Interleaving *two pairs* -- not two probes --
gives 1.65-1.70. Each probe is a dependent chain (position -> bitmap word ->
shift -> mask -> add) and at \|S\| <= 4 there is no ILP to find inside a pair:
the chain is three long and the pair ends. Consecutive pairs are wholly
independent, so two are always in flight.

**Iteration 6 (no improvement, and the measurement was wrong).** A width sweep
with `W` as a *runtime* parameter reported W=2 at 2.00 while the hand-written
2-way form measured 1.65 for identical work. The runtime W prevents unrolling and
`acc[]` never reaches registers. **This is exactly the array-vs-named-accumulator
mistake F3 documents for B x B, repeated one level up** -- the sweep was
measuring loop overhead, not ILP.

**Iteration 7 (improvement).** Compile-time width, and the sweep becomes
meaningful:

| W | ns/pair |
|---:|---:|
| 2 | 1.70 |
| 3 | 1.65 |
| **4** | **1.55** |
| **6** | **1.55** |
| 8 | 1.55-1.60 |

Four to six independent pairs saturate the scattered-load pipeline; past that,
nothing. **The unit that needs unrolling is the PAIR, not the probe** -- which is
only true because 89% of pairs are too short to unroll internally, a property of
the 1/i spectrum rather than of the kernel.

**Running total: 5.49 -> 1.55 ns/pair, 3.5x.**

### Iterations 8-10 — move work per-pair -> per-row, but watch the footprint

**Iteration 8 (improvement, 1.55 -> 1.32).** 42% of rows are singletons and each
takes part in N-1 pairings, so `v>>6` and `1<<(v&63)` were being recomputed ~4,000
times per row to yield the same two values. Hoisting them to load time makes the
probe a bare load-and-test.

**Iteration 9 (NO improvement, 1.50).** Generalizing the hoist to a full
`(word,mask)` list per row -- which also folds same-word positions at load time,
i.e. D2 run-collapsing paid once per ROW instead of once per pair -- **loses**.
Two numbers say why: the collapse ratio is only **1.25** (12,813 positions ->
10,226 entries), so folding removes almost nothing at this density, and the
entries cost **12 B against uint16's 2**. Trading a 6x larger working set for a
1.25x work reduction is a bad deal. D2 fails here for a different reason than it
failed as a per-pair kernel.

**Iteration 10 (improvement, 1.32 -> 1.15).** Iteration 9 diagnosed this one: if
footprint is what binds, hoist *less*. The universe is 5,008 bits, so a position
is 13 bits and fits one `uint16` -- 2 B/row instead of 12. Recomputing the shift
costs one instruction and wins back more than it costs.

| | ns/pair |
|---|---:|
| cross-pair ILP only (iter 7) | 1.55 |
| + singleton hoist, 12 B/row (iter 8) | 1.32 |
| + `(word,mask)` for all rows (iter 9) | 1.50 |
| **+ singleton as packed u16, 2 B/row (iter 10)** | **1.15** |

The chain is the finding: **hoist the invariant, discover the footprint binds,
then hoist less.** At N^2 pairs anything invariant in a row is recomputed N
times, so hoisting always reduces work -- but the hoisted table is touched N^2
times, so its *size* is on the critical path in a way the original computation
was not.

**Running total: 5.49 -> 1.15 ns/pair, 4.8x.**

### Iteration 11 — split the pair streams (biggest single win)

**1.15 -> 0.80 ns/pair**, identical across three runs.

42% of *rows* are singletons but **65.3% of pairs** have a singleton sparse side
-- singleton rows are the smaller side of nearly every pair they join, so they
are over-represented among pairs relative to rows. A 65/35 test inside the pair
loop is close to maximally unpredictable, and at ~1.15 ns/pair a single
mispredict is a large fraction of the budget.

Whether a row is a singleton is a property of the ROW, so it can be decided once
at setup. Partitioning the pair list into a singleton stream and a multi stream
gives two branch-free loops:

| W | 4 | 6 | 8 |
|---|---:|---:|---:|
| in-loop test | 1.10 | 1.10-1.15 | 1.20 |
| **split streams** | 0.85 | **0.80** | **0.80** |

Same principle as iterations 8 and 10 and the third instance of it:
**decide per row, never per pair.** At N^2 pairs a per-pair decision is made N
times more often than the data that determines it changes.

**Running total: 5.49 -> 0.80 ns/pair, 6.9x.**
