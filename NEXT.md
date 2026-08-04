# Plan forward — post-campaign, 2026-08-04

Written after the 13-round per-cell optimization campaign converged (all ten cells
closed, `results/OPTLOG.md`) and after a four-agent review of
`PROBLEM_STATEMENT.md`, `RESEARCH_PLAN.md`, the prior-art landscape, and the
remaining optimization surface.

**This document supersedes `RESEARCH_PLAN.md` §8's phase ordering.** The plan
below is what the *measurements* say to do next, which is not what the original
phase order says. Where they conflict, the reasons are stated.

---

## 0. The finding that reorders everything

The project was designed around this claim:

> Vectorize each cell of the pairing matrix. The asymmetric cells (B×S, B×R,
> S×R) carry the real-world benefit and vectorizing them is the core kernel
> result. (`RESEARCH_PLAN.md` §4, `PROBLEM_STATEMENT.md` §6 "Secondary")

**That is measured false.** Across all five asymmetric cells at all five corpus
shapes — 25 points — a SIMD-throughput kernel wins **zero** of them
(`OPTLOG.md` F5). `bs_neon_idx` and `br_neon` are correct, are in the tree, and
lose 0.51–0.86× everywhere. What wins is scalar loops, index lookups, search
strategy selection, and skipping.

The corrected claim, which the evidence does support:

> **Work avoidance beats work acceleration everywhere except where the work is
> irreducible.** SIMD wins in exactly one cell (B×B) and only when a cheap
> summary cannot prove the work unnecessary. The contribution is the summary
> structures and the bilateral selection that uses them — not the kernels.

Three consequences: the paper's central claim changes; Gate 2 has been passed on
its "scalar wins everywhere" branch (`RESEARCH_PLAN.md` §8 Phase 2 anticipated
this and said "the kernel contribution shrinks to selection + rank index —
reassess scope"); and **the selection layer, which currently fails its gate, is
now the whole result rather than a supporting component.**

---

## 1. Prior art — three claims must be repositioned

Audited against the literature. Full citations to be added to `LANDSCAPE.md`.

### 1.1 The zone map is NOT a new idea. Reposition it.

**BitFunnel** (Goodwin et al., SIGIR 2017) builds a hierarchical bit-sliced
signature index where higher-rank rows are the OR-reduction of lower-rank blocks,
and the query evaluator ANDs top-down, discarding any block whose higher-rank bit
is zero *before touching the dense bits underneath*. That is functionally the
same mechanism, published, in boolean document retrieval.

Also adjacent: **Column Imprints** (Sidirourgos & Kersten, SIGMOD 2013) — closest
*named* DB technique, but bins the **value domain** for single-column scan
pruning, where ours bins **bit position** for a two-row AND. Different axis,
different operation. And a **2024 GPU biclique-counting** paper (arXiv:2403.07858)
independently uses a "hierarchical truncated bitmap" to accelerate pairwise
neighbour-list intersection — convergent, contemporaneous, and it means we have
no priority claim on the idea.

**Honest position:** claim the *parameterization* (1 bit per 512, 0.195%
overhead) and the *application* — a pre-filter for all-pairs intersection
**cardinality**, where the popcount of the ANDed summary is the *exact* count of
bins needing a visit rather than a heuristic. Do not claim hierarchical
occupancy summaries. Cite BitFunnel prominently and up front; a reviewer who
finds it after we have claimed novelty will not be gentle.

### 1.2 The rank index is a 35-year-old primitive. The GAP is real though.

Range-popcount via `rank(b) − rank(a)` is textbook (Jacobson FOCS 1989; Clark
1996; Vigna WEA 2008 "rank9"; Zhou/Andersen/Kaminsky SEA 2013 "Poppy";
Pibiri & Venturini 2021). We are not contributing the primitive.

**But the gap is confirmed and specific.** CRoaring's own run×bitset cardinality
path (`run_bitset_container_intersection_cardinality` →
`bitset_lenrange_cardinality`, `include/roaring/bitset_util.h`) is a **scalar
word-by-word popcount over the run's span** — cost proportional to run *length*,
exactly the thing P4 removes. Nobody ships the rank-indexed version.

**Honest position:** "we apply a mature primitive to a case the compressed-bitmap
libraries still solve by linear scan, and measure when it pays (crossover ~256-bit
runs) and when it does not (below that it *loses* 0.5–0.78×)." Engineering
contribution, clearly framed. Not an algorithm.

### 1.3 Adaptive format selection has a large literature we have not cited.

`LANDSCAPE.md` has EmptyHeaded and the chemfp/RISC crossover. It is **missing the
entire SpGEMM auto-tuning literature**, which is closer to our framing than
either: **IA-SpGEMM** (Niu et al., ICS 2019) predicts format and algorithm per
multiplication from the density signature of the *inputs*; **OSKI** (Vuduc et al.,
2005) and **SPARSITY** (Im & Yelick) are the origin of auto-tuned sparse kernels.

**Honest position:** the novel part is (a) selecting jointly for **both sides of
a pair** rather than per operand, (b) across **five** representations including
WAH fills, and (c) in the **all-pairs N²** setting where the decision cost is
itself a first-order term. Not "adaptive format selection."

### 1.4 One claim we can now assert — CRoaring's mixed paths ARE scalar

`PROBLEM_STATEMENT.md` §5 claim 3 carried the caveat *"(Verify against current
CRoaring source before claiming this in writing.)"* — never done, until now.
Verified against current `master`:

| CRoaring path | Status |
|---|---|
| `bitset_container_and_justcard` | **SIMD** (AVX2 / AVX-512 / NEON) |
| `array_bitset_container_intersection_cardinality` | **scalar**, per-element loop |
| `run_bitset_container_intersection_cardinality` | **scalar**, per-run linear scan |
| `array_run_container_intersection_cardinality` | **scalar** merge/gallop |

Only the pure dense path is vectorized. The caveat can be removed and the claim
stated. **This also makes P5 substantially more winnable than assumed** and moves
"wire up CRoaring" from a chore to the highest-value experiment available.

---

## 2. What to do, in priority order

Ordered by (evidence value) ÷ (effort), not by the original phase numbering.

### P0 — the critical path

**N1. Wire CRoaring into the new harness as a first-class baseline.** *(2–3 days)*
Currently zero baselines exist in `bench/bench_cells.cpp` or `bench_select.cpp`;
they compare Storm against Storm. P1 and P5 are therefore **unfalsifiable as
written**, which is a fatal review problem independent of how fast we are.
CRoaring is already vendored at `third_party/CRoaring` and pinned at v4.7.2.
Highest value/effort ratio in this document, and §1.4 says we should win the
mixed cells decisively.
*Exit:* head-to-head ns/pair vs `roaring_bitmap_and_cardinality` across the five
corpora and the full density sweep, both spectra, in `results/`.

**N2. M3 — tile-hoisted selection. Fix Gate 1.** *(3–5 days)*
Gate 1 fails: selection costs 13.3 ns/pair = **28.3% of runtime against a 2%
budget**, regret 94.1%. `RESEARCH_PLAN.md` §8 Phase 1 prescribes exactly this
remedy for exactly this case and it has never been attempted. Every downstream
claim depends on it. Three sub-steps, cheapest first:
  1. Sort/partition rows by cardinality so a tile shares one pairing decision.
  2. Decide once per tile from tile-aggregate metadata; measure amortized cost.
  3. If still >2%, fall back to a static per-tile schedule and re-scope P2
     honestly as "per-tile, not per-pair."
*Exit:* selection ≤2% of runtime, or a documented re-scope of P2.

**N3. Kill the function-pointer dispatch on the hot path.** *(2–3 days)*
Every kernel call goes through `fn_bb`-style pointers (`kernels/storm_cells.h`),
which blocks inlining of a three-instruction body and is named in `OPTLOG.md` F8
as an unattacked cause of the selection overhead. `AGENTS.md` says C++17 exists
in this project precisely for `template<Repr,Repr>` + `if constexpr` dispatch,
and that dispatch was never built. Directly complements N2.
*Note:* keep the pointer-based registry for the **benchmark**; the shipping path
should be templated. These are different builds of the same kernels.

### P1 — makes the results trustworthy at the scale we claim

**N4. Scale to DRAM residency.** *(1–2 days — mostly re-running existing tooling)*
Every one of ~130 variants was measured on 1.5–6 MB, **L2-resident** corpora.
`PROBLEM_STATEMENT.md` §2 motivates 10⁷-bit universes — **1.25 MB per row**, so a
modest corpus is DRAM-resident. At least three conclusions are residency-dependent
and may invert:
  - F1 "SIMD-issue bound, not load bound" — derived at L2.
  - "Prefetch never helps" — the hardware prefetcher had headroom at L2.
  - **"Register blocking will not pay on NEON"** — this rests on loads not being
    the binding resource, which is a statement about L2, not about DRAM.
*Exit:* the sweep re-run at m ∈ {16k, 156k} words with residency labelled, and
any inverted conclusion corrected in `OPTLOG.md`.

**N5. The §6 API — it does not exist.** *(2–3 days)*
None of `STORM_allpairs_sum`, `_tiles`, `_threshold`, `_matrix` are written.
`RESEARCH_PLAN.md` §6 calls the batched primitive "the deliverable" and §11 makes
it a hard requirement for Definition of Done. It is also the interface contract
with Tomahawk (§8b). Build `_sum` and `_tiles` first — §3.5 shows the interesting
regime is output-bound, so the forms that never materialize N² are the ones that
matter.

### P2 — completeness, and one real risk

**N6. Zone-map extensions the per-cell loop could not reach.** *(3–4 days)*
Only one bin width (512 bits) was ever tested, and the summary is only wired into
B×B, B×S (where it loses, F9) and B×W. Untried:
  - bin-width sweep — may close F8's unexplained 28% dense-corpus overhead;
  - **two-level zone map** (summary over the summary) — at 10⁷-bit universes the
    O(m/512) scan stops being negligible and the trick that fixed rank applies
    again;
  - zone map as the **tile-partitioning signal** for N2, rather than an in-kernel
    gate — this is probably how N2 should be implemented.

**N7. Cross-ISA — the whole kernel layer is NEON-only.** *(needs x86 hardware)*
Every vector path is behind `#if defined(__ARM_NEON)`. Two of the plan's four
B×S designs (D1 gather, D3 `VPCONFLICTD`) are **unimplementable on NEON and
untested anywhere**, and the register-blocking argument (§3.2) was designed for
AVX-512's dedicated popcount port and has only ever been refuted on an ISA
without one. Several negative results in `OPTLOG.md` are ISA-local and should be
labelled as such until an x86 run exists.

**N8. Threading (§5.3, Phase 6).** *(3–5 days)*
Zero work-splitting code exists. Deliberately deferred, correctly — but it is the
last thing between the current numbers and a wall-clock claim, and F7's 10–70×
per-pairing cost spread means naive row partitioning will load-balance badly.

### P3 — hygiene, cheap, do alongside

**N9. Reconcile the documents with the measurements.** *(1 day)* — §3 below.
**N10. Prune the variant registry.** *(half a day)* ~130 variants; the threshold
sweeps (`adapt_r*`, `adapt_b*`, `adapt_f*`, `skip_f*`, `neon_u{2,3,6,12,16,24}`,
`occ_sel{5,15,60,85}`) are the evidence trail, not distinct algorithms. Collapse
each cell to {reference, winner, one bracketing probe, labelled negative
controls}. Keep `neon_hs` and `neon_ld2` — they are cited negative results.
**N11. Landmines.** *(half a day)*
  - `storm.cpp` — `int i` compared against `uint32_t n_vectors` throughout the
    `STORM_wrapper_*` family; latent signed-overflow UB at large N.
  - `kernels/cell_wah.cpp` `Cursor::consume` — `fill_left -= k` with no bounds
    check; a bad `k` wraps `uint32_t` silently instead of failing loudly.
  - `cell_bs.cpp` / `cell_br.cpp` `g_inflate*` — grow-only `thread_local`
    buffers, never released; ≥1.25 MB per thread at 10⁷-bit rows.
  - `storm_gen.cpp` `gen_uniform` complement path walks the whole universe per
    row — makes corpus *generation* the bottleneck at 10⁷ bits.

---

## 3. Document corrections required

`PROBLEM_STATEMENT.md`:
1. **§2 / §3** — bitmap × bitmap is **no longer Θ(m)** in this codebase. The
   zone-map-planned variant is sub-linear (24.9× on long runs). The fixed-cost
   premise still describes the *naive* dense kernel and must say so explicitly.
2. **§2.3** — the pair-class table has no row for near-**1** density, where the
   complement is sparse and R×R wins 52.7×. `p` cannot express a phenomenon
   symmetric about ½.
3. **§6 priority table** — "Secondary: **vectorized** asymmetric kernels" is
   falsified (0/25). Rename to work-avoidance strategy selection. The "~1.7×"
   Tertiary figure is an unsourced tier-1 number; the measured accumulator-count
   gain is ~1.24–1.4×.
4. **§7** — P2 **refuted as measured**; P3 refuted as worded; P4 proven with a
   crossover correction; P1 and P5 **untested**; and the "15-cell matrix" of P5
   does not exist — Roaring cells were never built, only the 10 non-Ro cells.
5. **The zone map appears nowhere in the document** and is arguably the strongest
   mechanism found. Needs its own section, including F9 (it does *not* generalize
   to B×S).
6. **§5 claim 3** — remove the "verify before claiming" caveat; it is verified
   (§1.4 above).
7. §2's 10⁴×/10⁵× haplotype figures are extrapolation from a napkin calculation,
   never measured at that scale. Label tier-1 per the project's own evidence
   ladder, sitting as they do beside tier-3 numbers in §2.5.

`RESEARCH_PLAN.md`:
8. **§1's cell status table is stale** — B×R, S×R, B×W, S×W are marked "does not
   exist"; all four exist and are closed. B×S is marked "scalar only,
   unvectorized"; it has NEON variants and they lose, which is the finding.
9. §8's phase ordering is superseded by §2 of this document.
10. README still quotes **114 GB/s** unqualified in its table (§7.1 flagged this
    as a Phase 0 fix; only a caveat box above it was added).

---

## 4. What the paper is now about

Original framing: *faster kernels for all-pairs set-intersection cardinality via
a vectorized representation-pairing matrix.*

What the evidence supports: **a study of when work can be avoided rather than
accelerated in all-pairs boolean intersection — with the measured result that
vectorization matters in exactly one of ten representation pairings, and cheap
summary structures (0.195% space) plus bilateral representation selection matter
in the other nine.** The negative results are a substantial part of the
contribution: Harley-Seal, D2 run-collapsing, prefetch, register blocking, and
SIMD in the asymmetric cells all measured and refuted, each with a number.

That is a more honest paper and a more interesting one. It is also a *smaller*
claim than the original, and it is not yet supported end-to-end: **N1 (baselines)
and N2 (Gate 1) are what make it a paper rather than a collection of
micro-benchmarks.** Neither is optional.
