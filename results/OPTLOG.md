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
| **B × B** | **8 — CLOSED** | `occ_sel` / `occ` | r8 `occ_sel` |
| **B × W** | **7 — CLOSED** | `occ` / `skip` / `rank` by corpus | r8 `occ` |
| **B × R** | **6 — CLOSED** | `scalar` / `rank` / `hybrid_pair` by corpus | r7 `hybrid_pair` |
| **B × S** | **5 — CLOSED** | `ilp8` / `shift` (within ~5%) | r6 `ilp16x` |
| **S × R** | 3 | `adapt_b8` / `merge_bl` | r10 `adapt_b8` (1.24×) |
| **S × S** | 3 | `adapt_r3` / `adapt_r24` | r10 `adapt_r24` (1.15×) |
| **R × W** | 2 | `skip2` / `merge2` | r7 `adaptive` |
| **W × W** | 2 | `skip2` / `skip` | r7 `skip2` |
| **R × R** | 0 | `adapt_r6` | r11 `adapt_r6` (1.06×) |
| **S × W** | 0 | `adapt_f1` | r11 `adapt_f1` (1.07×) |

**Four of ten cells are closed.** Round 11 sampled 17 further threshold and
unroll settings across every open cell and only two cleared the 5% noise floor —
S×W `adapt_f1` at 1.07× and R×R `adapt_r6` at 1.06×, both barely. That is what
exhaustion looks like: round 10's threshold sweep found the optima, and round 11
confirmed they are optima by failing to beat them from either side.

**B × S closed first** at 5 consecutive non-improving iterations: `ilp12` and
`prefetch64` joined `ilp_cache`, `prefetch_deep` and `occ` in failing. The cell
sits at ~0.80–1.23 cycles per list element against a 0.67 load-port floor, its
top three variants are within ~5% of each other on every corpus, and eleven
distinct hypotheses have failed to move it. The residual gap is the scattered
bitmap load, which no restructuring of the loop can remove.

**F10 — every hardcoded threshold in the project was mistuned.** Round 10 did
nothing but sample the constants — gallop ratios, rank crossovers, fill lengths,
zone-map selectivity — and won in **seven of ten cells**. Not by large margins
(1.0–1.3×), but systematically, and in cells whose kernels had already been
iterated on for several rounds.

That is a result about the project rather than about any cell. `RESEARCH_PLAN.md`
§5.1 argues that thresholds belong in a calibrated cost model (M4) rather than
in source, and §5.1's case has until now been an argument. It is now a
measurement: hand-picked constants lose to sampled ones essentially everywhere,
and the sampled optimum differs by corpus, so no single constant is right. The
honest conclusion is that further hand-tuning of these numbers is not worth
doing — **M4 should be built instead**, and the per-cell loop is hitting
diminishing returns for exactly the reason the plan predicted.

Round 9 added two hypotheses and both failed, which is what a converging search
looks like: `ss_neon16` (16×16 block compare) never wins, so the block-width
series saturates at 8 — past that the `vextq` rotations grow faster than the
comparisons saved. `rr_gallop_sym` never wins either: symmetric galloping was
the largest S×S win but does not transfer to runs, because run arrays are
already short enough that the merge's linear scan beats a search.

**The loop has not converged.** Only B×S is close to the stopping rule; the zone
map (round 7–8) reset most of the others by winning. That is the rule working
as intended — a productive idea should restart the search — but it means several
more rounds are needed before any cell but B×S can be declared done.

**Where the remaining headroom looks like it is.** The zone map transferred to
B×B (25×) and B×W (1.02× on long fills) but NOT to B×R (never wins) or B×S
(0.23–0.59×), which is F9's prediction holding: summaries pay where a dense
representation is scanned, not where a sparse one is walked. The untested
direction is a *second level* of summary (a zone map over the zone map) for
universes large enough that m/512 is itself expensive — irrelevant at the
65,536-bit universes benchmarked here, potentially decisive at the 10⁷ bits
`PROBLEM_STATEMENT.md` §2 motivates.

---

## M4 — the cost model, and GATE 1 currently FAILS

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
