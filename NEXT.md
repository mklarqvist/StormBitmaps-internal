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

## 2. Status after ten iterations (2026-08-04)

The pareto front of low-hanging fruit is **exhausted**. Everything below with a
good value/effort ratio has been done; what remains is genuinely expensive or
genuinely low-value, which is the stopping condition.

### Done

| # | Item | Result |
|---|---|---|
| **N1** | CRoaring baseline | **1.7–19.1×** over a `run_optimize`-tuned CRoaring on 4 hosts. P5 holds |
| **N7** | Cross-ISA | 4 microarchitectures: NEON, SVE, SVE2, AVX-512 |
| **N2** | M3 tile hoisting | **Gate 1 PASSES** — selection 28–48% → 0.10–0.48%. P2 holds |
| — | Probe-and-commit | Fixes the decision-quality problem hoisting exposed (0.66× → 1.09×) |
| **N4** | DRAM residency | **F11**: work avoidance scales *up* (8.8→20.5×), SIMD scales *down* (1.52→1.17×) |
| — | AVX-512 kernel | Dense B×B **10×**; 5× loss → 1.21× win; 0.125 c/word ceiling tier-1 → tier-2 |
| **N5** | §6 API forms 1–2 | `allpairs_sum`, `allpairs_tiles` shipped |
| **N9** | Doc reconciliation | PROBLEM_STATEMENT, RESEARCH_PLAN, LANDSCAPE §9 |
| **N10** | Variant pruning | 179 → 88 |
| **N11** | Landmines | 4 fixed, incl. a heap-buffer-overflow ASan found that 2.6 M correctness checks could not |

### Remaining — none of it low-hanging

| Item | Effort | Why it is not on the front |
|---|---|---|
| **Threading** (§5.3, Phase 6) | 3–5 d | Real work, and F7's 10–70× per-pairing cost spread means naive partitioning load-balances badly. Deliberately late so single-thread numbers stay comparable |
| **Cross-pair register blocking** (L1/L2, §2) | 5+ d | F11 says this is where the DRAM win is, but it needs a new kernel signature — every kernel is `(view,view)→count`. Architectural, not incremental |
| **Regret for the probe policy** | 2 d | Needs a true per-pair timing oracle over the batched driver. Worth doing before the write-up; not before threading |
| **§6 form 3** (thresholded) | 2 d | Only matters once a pruning bound attaches, which §6 lists as a non-goal until after Gate 2 |
| **Community mechanism** (§9.1) | 2–3 d | schema.json, plot.py, `--submit`. Valuable for the paper's cross-ISA story, but we now have 4 ISAs ourselves |
| **Two-level zone map** | 2–3 d | Only binds at universes where m/512 is itself expensive — beyond what is currently measured |
| **AVX2 / SVE2 kernels** | 3–5 d each | The portable path already runs there; these are optimizations, not gaps |

### The one thing to do next

**Write the paper.** The evidence base is sufficient and further kernel work has
stopped producing findings that change conclusions. Iterations 1–10 produced two
results that moved the thesis (F11's opposite scaling, and probe-and-commit
weakening the cost model's importance) and the last three produced none.

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
