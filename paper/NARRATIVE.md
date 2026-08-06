# StormBitmaps — Paper Narrative

Orchestrator document. Fixes the thesis, the claim ledger, the section skeleton, and the display-item
inventory before any prose is written. Authority order: `PROBLEM_STATEMENT.md` for scope,
`RESEARCH_PLAN.md` §16 and §20–24 for the current framing, and the final measurement campaign — now
in flight — for every number and figure that reaches the page.

**Companion documents, all in this directory.** [`SECTION_SPEC.md`](SECTION_SPEC.md) — what each
section must contain, paragraph by paragraph, with typed slots where numbers go.
[`DISPLAY_ITEMS.md`](DISPLAY_ITEMS.md) — panel-by-panel figure and table specification, data
schemas, captions, and the one palette the whole paper uses.  [`GAPS.md`](GAPS.md) — the adversarial
review: what a hostile reviewer rejects this on, and what defuses it. Read this file first; the
others expand it.

**Note on section numbers.** This document was renumbered after the companion documents were
drafted, so they cite the earlier scheme in places. Section *titles* are unambiguous; resolve any
mismatch with this crosswalk.

| Cited as | Now | Title |
|---|---|---|
| §2b | **§3** | The argument, end to end |
| §3 | **§4** | Framing rules for the drafters |
| §4 | **§5** | Claim ledger |
| §5 | **§6** | Section skeleton |
| §6 | **§7** | Display items |
| §7 | **§8** | Citations |
| §7b | **§9** | What the code actually is |
| §8 | **§10** | The honest problem |
| §9 | **§11** | Reconciliations |
| §10 | **§12** | Gap register |

§1 and §2 are unchanged. A reference to "§3" in a companion document means *Framing rules* if it
concerns the foil or the framing device, and *The argument, end to end* if it concerns the running
argument — the surrounding sentence disambiguates.

**Status of the source documents.** `PROBLEM_STATEMENT.md`, `AGENTS.md` and `CLAUDE.md` describe the
project as it was framed before Phase 2 measured. `RESEARCH_PLAN.md` §16 supersedes §14, and §23
(C32) retracts §22 (C31). Where they disagree, the highest-numbered section of `RESEARCH_PLAN.md`
wins, because in every recorded case the later text exists specifically because a measurement forced
it. This document is written against that latest state.

---

## 1. Venue, type, budgets

**Target:** arXiv preprint → *Software: Practice and Experience*. This is the venue family of both
Roaring papers (Chambi et al. 2016; Lemire et al. 2018) and of the popcount lineage, and SPE
explicitly publishes engineering-and-evaluation work — Roaring's own 2018 paper is titled
"Implementation of an Optimized Software Library."

**Written in the Nature Methods functional register** per the requested skills, rendered in the
existing `wlscirep.cls` scaffold. Concretely that means:

| Constraint | Value |
|---|---|
| Introduction | **no heading** — opens directly after the abstract |
| Skeleton | Introduction → Results → Discussion → Methods |
| Results | topical subheadings, one claim each |
| Discussion | **no subheadings** |
| Abstract | ~150–200 words, unreferenced, cites no display item |
| Main display items | **6** |
| Main text | ~4,000–5,000 words excluding Methods, references, legends |
| Extended Data | **does not exist** at this venue — do not plan around it |
| Supplementary | one organized document, own heading structure, every item cited by name |

SPE tolerates a longer main text than Nature Methods' 3,000 words, and this paper needs it: ten
kernel cells, four microarchitectures and seventeen corpora do not compress below about 4,500 words
without becoming a table dump. Budget the extra words to Results, not to Introduction.

**Methods budget — corrected 2026-08-06.** The ~2,500–3,000 figure above was inherited from the
Nature Methods skeleton and is wrong for this paper. Three rules cannot hold at once: every equation
and threshold lives in Methods and nowhere else; Methods fits 3,000 words; and evidence is never
culled. The formal treatment alone is ~4,000 words of statements and hypotheses.

**Resolution: raise the Methods budget to ~7,000 words and keep the other two rules.** SPE imposes no
Methods limit, and the venue's own Roaring paper carries a long one. This paper's contribution is
substantially analytic, so a Methods section that is mostly stated results with their hypotheses is
the honest shape for it, not an overrun to be managed. Proofs, worked examples and secondary
propositions still live in Supplementary; Methods keeps statement, hypotheses and pointer.

Do **not** resolve this by moving the cost model into Supplementary and having Results cite equations
that live there — that inverts the tier system and makes the main text unreadable alone.

---

## 1b. Scope — REFRAMED, and this supersedes everything written before it

> **The all-pairs framing is retired.** All-pairs sparse bitmap intersection is an ultra-niche
> problem: it required us to justify why anyone computes N² intersections before a reader would care
> about anything else. **Generic set algebra is widely useful; that narrow access pattern is not.**
> The object of study is now:
>
> **Set algebra over compressed integer sets — union, intersection, difference, symmetric
> difference, complement, together with their cardinalities and predicates — computed by selecting
> the best-matched pair of representations for the operands and for each region within them.**
>
> Two sets, arbitrary. Any operation in the algebra. This is *exactly* the setting Roaring occupies,
> which is the point: it makes the comparison direct, on the operation set the field already
> benchmarks, rather than on a batch API nobody else offers.

**Why the wide frame is the correct one, not merely the more marketable one.** Set algebra over
integer sets is a universal primitive rather than an application: inverted indexes in search engines,
bitmap indexes in columnar and OLAP databases, adjacency and neighbourhood queries in graph
analytics, permission and capability masks, feature and category memberships, sparsity patterns in
sparse linear algebra, and — as one domain among many rather than the motivation — variant sets in
genomics and fingerprints in chemoinformatics. Every one of these evaluates unions, intersections
and differences at scale, and every one of them has heterogeneous data. The representation question
is therefore universal, and a paper about it should be readable by any of those audiences without
first accepting a niche access pattern.

The narrow framing also cost us three things that now dissolve. It made every comparison against
CRoaring slightly apples-to-oranges, since CRoaring has no batch API. It made the artifact a batch
entry point rather than the drop-in-shaped library the project's own §16 argues for. And it put the
paper's applicability at the mercy of whether a reviewer believes anyone needs N² intersections.
Under the wide framing the artifact *is* the Roaring-shaped thing: a set-algebra library over
adaptively represented sets.

**The one-sentence pitch this buys.** *Every compressed-set library commits to a representation at
ingest; we choose it per operand pair, per region, from measured properties, across the whole
algebra — and the choosing is nearly free.*

**What survives unchanged.** The pairing matrix, the representations, the U-shaped cost map, the
work-avoidance finding, the zone map and rank index, the container-census mechanism, the real-corpus
evaluation, and the storage results. None of these depended on the batch setting.

**What changes.** The batch/all-pairs case becomes an important *special case* rather than the
frame — and it keeps the tile-hoisting and probe-and-commit contribution, because the cost of
deciding scales differently there (§3, "the two selection regimes"). And the paper must now cover
the full operation set rather than intersection cardinality alone, which is a genuine evidence gap
(**[GAP 11]**, the largest one open).

**Cardinality stays privileged, but is no longer the whole story.** Cardinality-only queries admit
shortcuts that materialization forbids — above all the rank-index identity that answers a run in
constant time without touching its bits. That remains a real differentiator and it should be
presented as one specialization among the operations, not as the paper's scope.

---

## 1c. Title

**Working title: *Work avoidance for efficient sparse set algebra*.**

The two content terms are the mechanism (**work avoidance**) and the object (**set algebra**), with
**sparse** naming the data regime in which compressed representations are worth having at all.
**Storm** stays out: the name belongs to the implementation, not to the contribution, and an
implementation name in the title narrows a claim meant to be general. The artifact is named in the
abstract, in Methods and in the availability statement.

> **Why *sparse* is exactly the right word, and why the U-curve is a theorem rather than a
> finding.** An earlier draft of this section treated the title's *sparse* as being in tension with
> R1's "the operative variable is distance from density one half". It is not, and the reason is
> worth stating precisely because it improves R1.
>
> Complementation is an involution on density: a set $X$ over a universe of $m$ bits with
> $|X| = k$ has $|X^{c}| = m - k$, so $d \mapsto 1-d$. Any representation costing $\Theta(|X|)$ on
> $X$ costs $\Theta(m-k)$ on $X^{c}$, and a system free to compute on either pays
> $\Theta(\min(k,\,m-k))$. Define **effective sparsity** $s = \min(d,\,1-d) \in [0,\tfrac12]$.
> Cost is monotone in $s$, and $s$ is symmetric about $d = \tfrac12$ **by construction**.
>
> So the U-shape is not an empirical discovery — it is forced by the algebra, and a set with one bit
> set and a set with all-but-one bit set are equally sparse in the only sense that matters. What the
> measurement contributes is **where the crossover in $s$ lies** and **how large the win is** on
> either side. R1 should be written that way: predict the symmetry, then measure the crossover and
> the magnitude. That is a stronger subsection than reporting a surprise, and it removes any reading
> in which the title contradicts the results.

**The same quantity bounds how much the metadata leaves undetermined, which turns the mid-density band from a
limitation into a theorem.** Given only the operand cardinalities --- the cheapest metadata any
selector has --- the answer is confined to

$$\max(0,\,a+b-m) \;\le\; |A \cap B| \;\le\; \min(a,\,b),$$

whose width simplifies to $\min(a,\,b,\,m-a,\,m-b) = m\cdot\min(s_A,\,s_B)$ --- the size of the
**smallest of the four descriptors** $A$, $B$, $A^{c}$, $B^{c}$. Two of those four are complements,
which is why a complement view is structurally necessary rather than a dense-tail optimisation. The
general form is also stronger than the equal-density case: a pair is cheap when *either* operand is
extreme, not only when both are.

For $a=b=k$ that interval has width $k$ when $k \le m/2$ and $m-k$ when $k \ge m/2$: width
$= m\cdot\min(d,1-d) = m\,s$. **The uncertainty in the answer is exactly proportional to effective
sparsity, grows linearly from one set bit up to density one half, and is maximal there.**

**The two ends of that interval fail to pin the answer for opposite reasons, and the dense end is
pure pigeonhole.** Below density one half the lower bound is zero --- the operands *may* be
disjoint, and typically are --- while the upper bound $\min(a,b)$ grows, so the interval widens.
Above one half the lower bound lifts off zero: two sets of density $d$ over $m$ positions cannot
avoid each other once they are more than half full, and

$$|A \cap B| \;\ge\; a + b - m \;=\; (2d-1)\,m .$$

At $d = 0.51$ that is $0.02m$: **two per cent of the universe must overlap, whatever the sets are.**
The floor rises at twice the excess density while the ceiling $\min(a,b) = dm$ rises only at $d$, so
the interval narrows, to width $m(1-d)$ --- and the answer becomes determined again, from above, for
exactly the reason it was determined below: there is almost no freedom left. That is the U, derived
rather than observed, and it is why the dense tail is as exploitable as the sparse one.

Two consequences, and both are worth stating in the paper:

1. **At the tails the metadata nearly determines the answer**, so there is a great deal of work to
   avoid --- and in the limit ($s \to 0$, or a zero zone-map AND) the answer is settled without
   reading any data at all. This is *why* work avoidance pays where it pays.
2. **At density one half the metadata determines almost nothing** --- the intersection may be empty
   or complete --- so in the generic case a selector reasoning from cardinalities alone has nothing to
   act on. **This bounds what these statistics identify; it lower-bounds no algorithm's work.** The mid-band where $\cell{\rB}{\rB}$ wins is therefore not a gap in the method but a
   **metadata floor**: a limit on what *these statistics* determine, reached where they determine
   least.

   > **Two guard-rails, both from the formal treatment.** First, call it a *metadata* or
   > *identifiability* floor, **never an information-theoretic floor** --- the interval width bounds
   > what cardinalities identify, and lower-bounds no algorithm's work. Nothing here is a complexity
   > result, and claiming one would be the single easiest thing for a reviewer to demolish. Second,
   > the irreducibility holds **in the generic case only**: a row equal to $[0,m/2)$ sits at
   > $d=\tfrac12$ with one run, and this project's own long-run corpora would contradict the
   > unqualified statement. Carry the qualifier every time.

**This also unifies the three side-information mechanisms of Fig.~1c--e.** Each is a device for
narrowing that interval more cheaply than computing the answer: cardinality bounds it globally, a
zone map bounds it per bin (and proves it zero when the AND is empty), a rank index resolves a run's
contribution exactly. **The work a pairing must do is proportional to the residual uncertainty after
the side information has been consulted** --- which is the single sentence the whole design follows
from, and the paper should say it once, early, and then let every later result refer back to it.

**The worst case bounds what is possible; the expected case says what is typical, and it points the
same way.** For two sets drawn over a universe of $m$ positions with $|A|=a$, $|B|=b$:

$$\mathbb{E}\,[\,|A \cap B|\,] = \frac{ab}{m} = m\,d_A d_B,
\qquad
\Pr[\,A \cap B = \emptyset\,] = \frac{\binom{m-a}{b}}{\binom{m}{b}} \;\approx\; e^{-ab/m}.$$

Two single-bit sets over a universe of width $W$ therefore overlap with probability exactly $1/W$,
and the expected intersection *density* is the **product** of the operand densities --- so it falls
quadratically as the operands get sparser while the cost of a dense kernel stays flat.

**The consequence is the one that matters for the design.** When $ab \ll m$, $\Pr[\text{disjoint}]
\to 1$: in the sparse regime, an empty result is not an edge case, it is the *typical* case. This is
exactly what the tile pipeline observed empirically --- \nm{fraction of sampled pairs with an empty
intersection in the target regime} of sampled pairs had $|A \cap B| = 0$ --- and it explains why
proving disjointness cheaply is the single highest-value thing a summary can do, rather than one
trick among several.

So the two analyses agree and cover different questions:

| | quantity | behaviour | what it justifies |
|---|---|---|---|
| **worst case** | interval width $= m\,s$ | linear in $s$, peaks at $d=\tfrac12$ | the mid-band floor: metadata determines least, so a cardinality-only selector has nothing to act on |
| **expected case** | $\mathbb{E}\,|A\cap B| = m\,d_Ad_B$; $\Pr[\emptyset] \approx e^{-ab/m}$ | falls quadratically as operands sparsify | why disjointness-proving dominates in the sparse regime |

Together they predict the whole shape of the results before any kernel is written: a generically unexploitable
middle, tails where the answer is nearly determined *and* nearly always empty, and a cost curve
symmetric under complementation. **The measurements then establish where the crossover falls and how
close real kernels come to the floor** --- which is a much stronger framing than presenting the same
curves as discoveries.

*Status: analytic. The bound is elementary and must be presented as a derivation, not as a
measurement; what is measured is where the crossover sits and how closely real kernels approach the
floor.*

> **Three corrections from the adversarial verification pass, all of which tighten claims that were
> stated too broadly here first.** Full record in [`THEORY_REVIEW.md`](THEORY_REVIEW.md).
>
> 1. **"Under randomness, selection would buy only a constant" is false as stated, and the error is
>    instructive.** It holds among $\rS,\rC,\rR,\rW$ only. $\rB$ is *in* the matrix, and it is beaten
>    by roughly $1/(w\,s)$ — at $s=10^{-6}$ the spread across the matrix is over four orders of
>    magnitude. Scope the claim to the compressed representations; the bitmap being beaten by an
>    unbounded factor is the paper's headline, not a footnote to it.
> 2. **$\mathbb{E}[r] \le m\,s$ is off by one at the dense corner.** The correct sandwich is
>    $\tfrac12 m s \le \mathbb{E}[r] \le m s + 1$.
> 3. **The lower bound on $\Pr[\emptyset]$ needs $a+b\le m$**, which was unstated.
>
> **And one gap that is a genuine hole, not a wording fix.** The formal results are about descriptor
> *length* ($|X|$, $r$, $\ell+f$); the thesis is about *cost*. Nothing numbered crosses that bridge —
> the "a run answers in two lookups" step appears only as an unnumbered worked example, so the
> rank-index result is currently carried entirely by measurement. **Add a proposition stating that
> with a rank index $\cell{\rB}{\rR}$ costs $2r$ lookups, independent of $m$, of $|B|$ and of run
> length.** It is elementary and exactly true, and without it the chain from "shorter descriptor" to
> "less work" has no formal step in it.

**A structural consequence worth putting in the paper: this explains the "accidental" dense-tail
win.** Run count is nearly invariant under complementation --- $|\mathrm{runs}(X^{c})|$ differs from
$|\mathrm{runs}(X)|$ by at most one --- so $\rR$ is *intrinsically* complement-symmetric and
$\cell{\rR}{\rR}$ wins the dense tail with no complement machinery at all. A sorted array $\rS$
is not complement-symmetric: $|X^{c}|$ is huge when $|X|$ is small. That asymmetry is precisely why
an explicit complement view is needed for the array-based cells and not for the run-based ones.
[GAP 1] recorded the dense tail as "won incidentally by $\cell{\rR}{\rR}$"; it was not an accident,
it is a property of the representation, and saying so converts a weak spot into an explanation.

Alternatives held in reserve:

- *Work avoidance for compressed set algebra* --- **compressed** narrows the domain without any
  tension against R1, and matches how this literature names itself (compressed bitmaps). Take this
  if the R1 sentence above proves awkward to write.
- *Work avoidance for faster set algebra* --- states the payoff rather than leaving it implied.

Avoid any title built on "the pairing matrix" — it is our internal term, it means nothing to a
reader who has not read §3, and a title must not require the paper to explain it. **Do not put
"all-pairs" in the title.**

---

## 1d. Narrative arc and novelty position — REFRAMED 2026-08-06, supersedes conflicting text below

Five novelty claims were adversarially fact-checked against the literature on 2026-08-06 (one
subagent per claim, each instructed to refute rather than confirm). **Three were refuted and two
were partially covered.** The corrected position is recorded here because it changes what the paper
may claim, not what it may report. Nothing measured is retracted.

### What the checks actually found

| Claim as posed | Verdict | Why |
|---|---|---|
| Analytic theory of why Roaring works | already covered | Wu, Otoo & Shoshani (LBNL-49626, 2004; TODS 31(1), 2006) §4.2 Eq. (1) is *algebraically identical* to THEORY.md Prop. 8's `W` row — both are `(m/p)[1−(1−d)^{2p}−d^{2p}]`, differing only in payload width `p=w` (EWAH) vs `p=w−1` (WAH). Symmetry about `d=½` is their §6. `E[r]=k(m−k+1)/m` is Mood (1940). |
| Container threshold is mistuned | partially | That 4096 minimises *storage* is the published design rationale, in the authors' words. The *compute* crossover (k≈64–128) is unmeasured anywhere public — that part stands. |
| Zone maps are novel here | already covered as a mechanism | Roaring's key array is an occupancy summary at 2^16; BitFunnel §4.1 higher-rank rows are near-exact; Lucene `SparseFixedBitSet` is one occupancy bit per 64-bit word. |
| Bounded queries are novel here | partially | Size bound is Swamidass–Baldi on fingerprint bitmaps; shipped as chemfp's BitBound over popcount-sorted fingerprints at N×N scale. Prefix filtering is Chaudhuri et al. (ICDE 2006); the `p(X)` formula is Xiao et al. Anastasiu & Karypis (2016) already combine both for Tanimoto on binary vectors. |
| Remembering block decisions is novel | already covered | CRoaring stores the decision: `roaring_array_t.typecodes` (`roaring.h:862`), `container_and` dispatches over stored tags (`roaring.h:5915`), and `lazy_or`/`repair_after_lazy` amortise per-block bookkeeping across a batch by design. |

### The position that replaces them — collegial, and it needs no invention claim

**This paper does not find fault with Roaring and must never read as though it does.** Roaring
minimises serialised size and is excellent at it. This work asks a different question: *what if
query performance is what matters, and some storage may be spent to get it?* That is a different
objective, not a better answer to Roaring's.

Two results follow, and they should be presented as two tracks because they have different costs:

**Track A — speedups at no storage cost.** Roaring carries a second constant of an entirely
different kind from the container threshold: `const int threshold = 64;  // subject to tuning`, at
four sites (`roaring.c:6956`, `:6995`, `:7021`, `:7041`), selecting merge versus galloping inside
array×array intersection. It is pure compute — no bytes stored, nothing serialised, no format
implication — and the authors flag it as untuned in the paper itself: *"We arrived at this threshold
(c₁/64 < c₂ < 64c₁) empirically as a reasonable choice, but it has not been finely tuned."* Tuning
it is a free win, upstreamable as-is, and it costs the user nothing. Note the contrast the library
itself draws: this constant is annotated *subject to tuning*; the container enum at `roaring.h:2486`
is not, because it is not that kind of constant. **The library already separates storage constants
from compute constants; it has simply never documented which is which.**

**Track B — larger speedups, paid for in storage.** Runtime container promotion, a sub-container
occupancy summary, and thresholded operation. This is where the 10–100× lives, and it is explicitly
a trade: query cost down, bytes up. State the exchange rate rather than burying it. (C33 must be
fixed before this is quotable.)

**The mechanisms in Track B are established elsewhere and absent from this lineage. The measured
gain is the evidence that the absence was costly and the transfer non-obvious.** That is a
legitimate, common, defensible contribution and it is what the measurements support. Show the
absence in CRoaring's own source rather than asserting it:

- **No occupancy summary below container granularity.** Zero occurrences of any such structure; the
  key array summarises at 2^16 and nothing summarises beneath it.
- **No size-bound pruning, with the statistic already in hand.** `roaring_bitmap_jaccard_index`
  (`roaring.c:17892`) reads `c1` and `c2` and then calls `roaring_bitmap_and_cardinality`
  unconditionally. The size bound needs only `min(c1,c2)/max(c1,c2)`; there is no threshold
  parameter in the API, so there is nothing to prune against. Six lines, and they make the point.
- **Rank exists but is not used to skip intersection work** — it serves `select`/`rank` queries.

**Never write that Roaring is handicapped, mistuned, or defective.** 4096 is the exact optimum of
the objective its authors stated ("To decide the best container type, we are motivated to minimize
storage"). Nobody characterised the constant a *query-cost* objective induces. That is a question
not asked, not a flaw missed — and it is the framing most likely to survive a reviewer who may well
be Lemire.

**Supporting evidence nobody has published.** RoaringBitmap/roaring (Go) PR #107 forced bitmap
containers for speed, measured 50× in production, and was reverted by Lemire in PR #331 on memory
grounds. The effect surfaced once, anecdotally, was traded away as a memory decision, and was never
characterised. Cite it by number: it makes "nobody investigated the consequence" concrete rather
than asserted, and it independently corroborates that the compute win is real.

### Architecture — state it correctly, it has been mis-stated twice

**Storm implements its own representations; it does not call CRoaring.** The cells
(`kernels/cell_bb`, `cell_bs`, `cell_br`, `cell_wah`, `cell_sparse`, `cell_comp`) are built against
Storm's own representation layer. `kernels/` contains **zero** references to Roaring. CRoaring is
vendored and used as the external baseline, exactly as `methods.tex` already states: "Roaring is
never constructed as a cell of this matrix; it is used throughout as an external baseline only."

So the paper is: **per-row/per-pair selection among B, S, R, W and the complement view — Roaring's
container types plus EWAH and the complement — benchmarked against Roaring.** That framing is
accurate and is the paper.

**Do NOT write that Storm wraps, embeds, or extends Roaring's containers as-is**, and do not promise
a deployment story in which a user keeps their existing `roaring_bitmap_t` and opts into a Storm
metadata layer. That would be a true and considerably stronger story, but it describes software that
does not exist in this tree. If it is built, this section must be rewritten before the claim is made.

The one place Storm *does* touch CRoaring is the baseline re-pricing pass
(`roaring_bitmap_storm_promote_arrays`, `third_party/croaring_modified`), invoked from
`bench/bench_baseline.cpp` and `bench/bench_allpairs.cpp`. That is a benchmark-fairness measure and a
demonstration that Roaring can be re-priced without touching its format — not an integration.

### The seven-step arc — the narrative spine

Chronological order is permitted here because the construction is genuinely staged: each step exists
because the previous one exposed a limit. `manuscript-architecture` requires the reason for the
order to be visible in the text; make it so.

1. **Test Roaring at scale** on the largest bitsets, under a query-cost objective rather than a
   storage one, and observe where time actually goes.
2. **Build a representation-pairing implementation** and measure a large margin over stock Roaring.
   **Report this as a diagnostic, not as the result** — step 4 explains why.
3. **Attribute the margin.** It is not a defect: the container rule is optimal for serialised size,
   which is the objective it was designed against. Under a query-cost objective the optimum sits
   elsewhere. Characterise both, and show they are limits of one parameterised objective 32–64×
   apart.
   **Report the non-confluence result here — it is measured, unpublished, and it is what makes the
   step non-trivial.** Lowering the insertion threshold directly, the obvious move, is *worse than
   doing nothing*: a 46% regression on `census1881` (1496 → 2186 ns/pair). CRoaring's run
   conversion is not confluent with container type — deciding whether to become a run compares the
   run encoding against the encoding the container currently has, which is `2c+2` bytes for an
   array but a fixed 8192 for a bitset. Promoting before run conversion changes which comparison a
   borderline container faces and flips containers into runs that are worse for AND-cardinality on
   weakly clustered data. The promotion must therefore run *after* `run_optimize()`. This is a
   property of the library's conversion rules, not of our pass, and it is the reason a
   compute-priced Roaring cannot be obtained by retuning one constant. Written up in Methods,
   "A compute-priced CRoaring baseline, and why it must be a second pass".
4. **Re-measure against Roaring at its best.** Apply the compute-oriented choice to CRoaring itself
   and re-run. Much of step 2's margin disappears; say so plainly. This is what makes every later
   number credible. Establish here that the container constant is fixed by the interoperable format
   — type is inferred from cardinality on read (`roaring.c:14421`, `:14553`; RoaringFormatSpec §2)
   and `bitset_container_validate` (`roaring.c:8263`) rejects deviation — so a compute-oriented
   choice must be a runtime, in-memory, never-serialised pass. This is *why* the remaining gains
   have to come from a layer above the container rather than from retuning it.
   Report Track A here too: the galloping constant is a free speedup with no storage or format
   consequence, and it is the one change upstream can take unmodified.
5. **Add an occupancy summary below the container.** Introduce as a known mechanism (BitFunnel;
   Roaring's own key array at coarser grain); claim the sub-container placement, the gating policy,
   and the cost model. Measure against the step-4 baseline, not against stock.
6. **Narrow the question** — thresholded and size-banded operation. Introduce as known (Arasu;
   Chaudhuri; Swamidass–Baldi; chemfp BitBound; Anastasiu & Karypis); claim the composition
   analysis. Frame as narrowing the *question*, not as a pairwise workload — do not re-enter the
   retired all-pairs framing.
7. **Assemble.** This is where the strongest unclaimed result lives: **the mechanisms do not all
   multiply.** They compose only when they act on different waste *and* read different operand
   statistics — which is why prefix filtering composes with a zone map and the size bound does not
   (verified: 4.44× → 1.02× under a decorrelation control). Do not demote this to a concluding
   paragraph; it is visible only at assembly, and it is the paper's best finding.

**The cost model is not a step — it is the connective tissue.** Pair every empirical step with the
model that predicts it. Without that, the arc reads as a tuning exercise on one corpus.

### Citation repairs — unconditional, required under any framing

- `wu2006wah` is **in `references.bib` and cited nowhere in the manuscript text**, while the paper
  re-derives its central equation. Cite it at THEORY/Methods as the source of the per-format
  expected-size model.
- **Tree-Encoded Bitmaps** (Lang, Beischl, Leis, Boncz, Neumann & Kemper, SIGMOD 2020) was absent
  and is now cited. It was read in full on 2026-08-06, which corrected two things the first
  fact-check got wrong. (i) The (density, clustering-factor) parameterisation is **not TEB's** —
  TEB cites Wu et al. 2006 for it, so attribute it there. (ii) **TEB is not a performance
  competitor and must not be treated as a baseline to beat.** Its own Fig. 17 puts it at ~2.85×
  and ~2.88× the time of a plain Boost `dynamic_bitset` on intersection; its AND is a scalar lazy
  run-merge that cannot produce a compressed result; its only SIMD is in the tree-traversal scan
  iterator; and Roaring beats it on read, intersect (1.6–1.9×) and update (1.8×). Its own
  conclusion proposes TEB as a *container type for Roaring*.
  **Cite it for what it concedes**, which supports this paper: an uncompressed bitmap reads faster
  than all three compressed formats over `16 ≤ f ≤ 128` and `0.01 ≤ d < 1`, and the authors
  "expect a performance-optimized implementation to dominate an even larger space" than their
  unoptimised baseline. Two caveats if quoting its numbers: the whole evaluation is at n = 2^20
  (cache-resident, residency never stated), and the intersect measurements cover exactly two
  (d,f) configurations.
- Size filter is **Arasu, Ganti & Kaushik (VLDB 2006)**, not Bayardo; prefix filtering is
  **Chaudhuri, Ganti & Kaushik (ICDE 2006)**, not Xiao (Xiao cites it as "[8, Lemma 1]"). Currently
  misattributed at `sections/supplementary.tex:76`.
- Add: Mann/Augsten/Bouros (PVLDB 2016), Anastasiu & Karypis (DSAA 2016), Sandes et al.
  (Inf. Syst. 2020), Morzy et al. (ADBIS 2003), Mood (1940), Fréchet/Boole.
- `1 − e^{−y}` is the `p/m → 0` limit, **not exact** (exact: `1 − C(m−p,p)/C(m,p)`), and it assumes
  uniformly scattered prefixes — the model the similarity-join literature explicitly abandoned,
  since frequency-ordered prefixes are what make the filter work. Present as a null-model gate, not
  a law. Drop "regardless of density, threshold or universe size."

### Bookkeeping

CRoaring PR #851 and issues #822/#823/#824/#841 are **this project's own 2026 filings**. They are
not independent prior art and must never be cited as such.

---

## 2. The thesis

**The one-line form, in the author's words, and the sharpest statement of it:**

> **"We don't have to optimize algorithms; we are simply optimizing ways of NOT doing anything at
> all."**

That is not a slogan for the same claim — it is a stronger claim, and it changes what the paper is
competing on. The object being optimized is **the decision not to compute**, not any kernel. Three
things follow, and the drafters must carry all three:

1. **The paper does not compete on kernel speed, and should say so early and without defensiveness.**
   Popcount is solved; the fastest AND-and-popcount loop is not the contribution and is not claimed.
2. **It dissolves the paper's most awkward-looking finding.** That throughput-oriented kernels win
   essentially none of the asymmetric measurement points is not a surprise requiring explanation —
   under this frame it is *confirmation that the optimization axis is avoidance, not speed*. It
   should read as "of course", once the frame is stated, rather than as a result that cut against
   expectation.
3. **Every analytic result becomes an answer to one question:** how much can be skipped, and how do
   we know before doing it? The null model says what is skippable when the data is structureless;
   the deviation from it says what is skippable in fact; the gates say when asking is cheaper than
   doing.

**Where SIMD belongs, and why this is not a paper against vectorization.** The two optimization axes
are complementary, not opposed, and the paper must say so or it will read as dismissing the field's
existing work. **Where work can be avoided, avoid it; where work is unavoidable, make it fast.**

The finding that throughput kernels win essentially none of the *asymmetric* measurement points is a
statement about the asymmetric cells only, and it has a plain cause: those cells are already
work-proportional to the smaller operand, so there is little left to accelerate. It is emphatically
**not** a claim that vectorization is useless.

The complement of that finding is equally load-bearing and the paper should give it equal weight:
**$\cell{\rB}{\rB}$ is where work is genuinely irreducible in the generic case, and it is exactly
there that throughput optimization pays.** This project's own record shows it — adding an AVX-512
`VPOPCNTQ` path took the dense cell from \nm{slow} to \nm{fast}, a \nm{large} improvement, and it was
the right investment precisely because that cell cannot avoid its work.

The mapping onto the analysis is clean enough to state directly, and Fig. 2d draws it: **the green
region is avoidance's domain and the red waist is SIMD's.** A reader should leave with "avoid what
you can, vectorize what you cannot, and the analysis tells you which is which" — not with "SIMD does
not help."

**The design, as one progression — and this is the paper's spine.** The mechanisms are not a bag of
tricks; they form a ladder ordered by **what the caller gives up**, and the more a caller is willing
to specify about what they want, the more can be skipped.

| # | Mechanism | Contract | Needs | What it buys |
|---|---|---|---|---|
| **1** | representation choice | unchanged; identical answer | nothing | compute in whichever equivalent encoding is cheapest |
| **2** | zone maps | unchanged; identical answer | metadata | skip regions that provably cannot contribute |
| **3** | prefix filtering, size and cardinality bounds | **narrowed** — only results above a threshold, or within a size band | the caller to say what they want | never enumerate the rest; discard pairs from two integers, touching no data |
| **4** | batch amortisation | unchanged | **many operations** | drop work that would otherwise repeat per pair |

> **Correction — the composition rule I first stated was too weak, and it was falsified.** I claimed
> mechanisms multiply when they act at *different granularities*. They do not. The size bound acts
> at the coarsest granularity of all and still fails to multiply with the rungs below it, because
> its survivors are pairs with $a \approx b$ while every mechanism below is cheapest when sizes are
> *dissimilar* — so it passes on exactly the pairs the rest serve worst. Verified: survivors carry
> $\mathbb{E}[\min(a,b)]$ about $4.2\times$ the corpus average at $K=10^4$, $t=0.9$, growing as
> $\ln K/2$. A control that decorrelates the size test at an identical survival rate restores the
> product to within $2\%$, which isolates the cause.
>
> **The corrected rule:** *mechanisms multiply when they act on different waste **and read different
> operand statistics**. Different granularity is necessary, not sufficient.* This explains both
> observations rather than one — prefix filtering reads element identity while a zone map reads bin
> occupancy, so those two do multiply; the size bound reads cardinality, which is the same statistic
> the per-pair costs already track, so it does not.

**Two boundaries, and each is load-bearing.** Rungs 1–2 are free in the sense that matters: the
caller asks the same question and gets the same answer, and only the work changes. **Rung 3 is not a
cheaper version of them — it answers a narrower question**, and is available only because the caller
said what they actually wanted. **Rung 4 is different again: the contract is unchanged, but it
requires many operations rather than one**, because what it removes is repetition.

**Rung 4 is what rescues the tile material the reframing appeared to orphan** ([GAP 13] §2). Tile
hoisting and probe-and-commit are not artefacts of the retired all-pairs framing — they are rung 4,
and rung 4 is a legitimate regime rather than a lost one. What repeats across a batch, and what rung
4 therefore drops: metadata built once per set and reused by every operation that set enters (the
ingest assumption of [GAP 13] §4); a selection decision hoisted to a tile so one decision serves
thousands of pairs; probe-and-commit, which pays for timing a handful and reuses the verdict; and
operand data loaded once and paired against many. Fig. 1f is exactly this — the cost of deciding
falls as the decision is shared, so **the decision and the metadata are amortised and only the
kernel work stays per-pair.**

Three obligations follow. **Exact set algebra is the default and rungs 1–2 stand alone as a complete
contribution** — the headline results must not depend on narrowing the contract. **Rung 3 is opt-in**,
with its bound stated, never folded in beside rungs 1–2 as though they were interchangeable. And
**rung 4 must be presented as a regime, not as the setting** — it applies when there are many
operations, it is the only rung the evidence covers well, and the paper should say both.

**The over-reach this invites, and the bound on it.** Do not let this become a claim that the method
never does work, or that avoidance is always available. The paper's own results bound it: there is a
band about density one half where the metadata determines least and a bitmap is genuinely the right
kernel, and every avoidance mechanism has a two-sided liability — declined when operands are too
dense because it proves nothing, and when too sparse because it costs more than the data it was
meant to avoid reading. The honest form is that **avoidance is the thing being optimized, and the
analysis says exactly where it is available and where it is not.**

---

One sentence, and everything in the paper serves it:

> **A dense bitmap costs the same no matter what it contains. Equivalent representations of the
> same set do not. This paper exploits that deviation --- selecting, per operand pair and per
> region, whichever equivalent representation makes the work smallest --- and so avoids work rather
> than accelerating it.**

Every word of that is doing something. **Equivalent** is the load-bearing one: these representations
encode the same sets and return the same answers, so nothing is approximated and no contract is
weakened --- only the cost changes. **Fixed cost** is what makes the opportunity provable rather
than hoped for: a bitmap pays $\Theta(m)$ whether it holds one element or all of them. And
**deviation** is the contribution: the other representations' costs are functions of structure ---
run count, cardinality, literal and fill count --- not of the universe, so on real data they finish
inside the gap the bitmap's flat cost leaves open.

The longer form, for the abstract and for anyone who needs the scope spelled out:

> This paper establishes that in set algebra over compressed integer sets, choosing which
> representation pairing to compute in --- cheaply, from measured rather than modelled properties ---
> dominates making any individual kernel faster, shown by a systematic measurement of the
> representation-pairing matrix across the boolean operation set, the full density range, four
> microarchitectures and seventeen real corpora, in which the kernels that win are the ones that
> skip work rather than the ones that process it faster, and no single representation pairing wins
> on more than a minority of corpora.

Three sub-theses hang off it, in decreasing order of how load-bearing they are:

1. **The composition is the contribution, not any component.** Zone maps are BitFunnel, rank/select
   is Jacobson, probe-and-commit is Micro Adaptivity, format dispatch is Roaring and EmptyHeaded.
   Every one has prior art and every one must be cited as such. What is unclaimed is a selector over
   a *full representation-pairing matrix*, gated on *measured* properties, evaluated as one system.
   This is exactly Roaring's own situation — array containers, bitset containers, run containers and
   SIMD intersection were all known — and Roaring is the correct model for how to claim it.

2. **The map is a result in its own right.** Which pairing wins at which density, measured rather
   than argued, including the cost of choosing wrong.

3. **Work avoidance dominates work acceleration in this regime, and the boundary between them is
   locatable.** This is a positive, transferable design principle, and it is stated as one.

**On incrementality, and the tone to take about it.** Every component here has prior art, and the
paper says so. That is not a weakness to be managed — it is the normal shape of the field, and
Roaring is the proof: array containers, bitset containers, run containers and SIMD intersection were
all known, and what landed was the composition and the packaging.

**Write it confidently, not apologetically.** The defensive form — *"we do not claim these mechanisms
are new"* — appears several times in this document because it is true, but it must not become the
manuscript's register. The confident form is: **these mechanisms are known; what is new is knowing
when each one pays, and that is the part nobody has written down.**

**The standard this paper is actually held to is not novelty but boundedness.** A weak incremental
paper reports a speedup. A strong one states the opportunity, proves when it exists, proves when it
does not, and shows why it cannot be had another way. Nearly every result assembled here is of the
second kind, and the drafters should present them as such:

- the null model is a *floor*, so real data can only deviate favourably;
- the mid-band about density one half is where the method correctly gets out of the way, with a
  stated reason (the metadata determines least there);
- every mechanism carries a two-sided liability, with both sides derived;
- the composition rule says when mechanisms multiply and when they substitute;
- and the incumbent is *repaired first* ([GAP 14]) and only then compared.

That last one is the strongest item in the paper, and it is strong precisely because the work is
incremental. Beating a mis-tuned baseline is an artifact. Beating a baseline you fixed yourself, and
explaining why the gap survives the fix, is a result.

**On negative results.** This project accumulated a large number of them — refuted optimizations,
losing variants, ablations that went nowhere. **They do not go in the paper.** A finding earns a
place only if it *contributes to the argument*, and the test is whether removing it would leave a
design decision unjustified. Three survive that test, and each is stated as the positive claim it
supports rather than as a failure:

| Survives as | Because it justifies |
|---|---|
| "work avoidance is the mechanism that wins" | the entire design — why the asymmetric cells are built around rank indexes and zone maps rather than vector width |
| "selection must be measured, not modelled" | probe-and-commit over the cost model |
| "the filter must be gated" | why work avoidance is applied conditionally |

Everything else — Harley-Seal, register blocking, prefetch behaviour, hashed filters, row
hierarchies, the losing per-cell variants — belongs in a blog post or, at most, one Supplementary
table nobody has to read. Do not build a Results subsection around them, do not give them a figure,
and do not use the word "negative result" in the manuscript.

**What the paper must not claim:** component novelty; a shipped, usable library (see §10); the
original 10⁴×/10⁵× haplotype figures (never measured at that scale); or any single headline speedup
number divorced from its corpus regime.

---

## 3. The argument, end to end

This is the paper told through as one continuous argument, in the order a reader meets it. Numbers
appear as typed slots `[NUM: …]`; display items appear as `[FIG n]` / `[TAB n]`; and where writing
the argument out exposed something we cannot currently support, it is marked **`[GAP n]`** and
collected in §12. The gaps are the point of this section — they were not visible in the claim
ledger and became obvious only when the argument had to run continuously.

---

**The setting.** Given N sets over a shared universe, compute `|Xᵢ ∩ Xⱼ|` for every pair. This is
the inner loop of similarity joins, of linkage-disequilibrium scans, of chemical fingerprint
screening, of graph co-occurrence analysis. Essentially every implementation computes it the same
way: represent each set as a dense bitmap and evaluate `popcount(A AND B)` with the widest available
vector instruction. That kernel is genuinely excellent, it has been optimized for two decades, and
the popcount problem underneath it is solved.

**The problem with the default.** Its cost is Θ(m), where m is the universe in words. Not Θ(|A|),
not Θ(|A ∩ B|) — Θ(m). It is the same cost whether both sides are half full or one side is empty.
For a single pairwise operation nobody cares. At N² pairs, this fixed cost *is* the computation, and
on real corpora most of it is spent ANDing zeros against zeros.

**Why that isn't a straw man.** The tempting response is that real data is dense enough for this not
to matter. It is not, and the reason is structural rather than anecdotal: real corpora are
*heterogeneous*. Row densities inside a single corpus span orders of magnitude. Human variant data
is the clearest natural case — the overwhelming majority of sites are rare while a thin tail of
common sites carries most of the allele-frequency mass — but web category postings, census
attributes and graph adjacency all show it. **A fixed representation is a bet that every row looks
like every other row, and on real data that bet loses.**

**What the field already does about it.** Roaring is the state of the art and it is built on exactly
this observation: partition the universe into 2¹⁶-bit chunks and choose per chunk between an array,
a bitset and a run container. It works, it is deployed everywhere, and this paper is not going to
out-engineer it at its own data structure.

**Where the opening is.** Roaring's adaptivity is *fixed in granularity and static in time*. The
chunk width is a constant, the container threshold is a constant (cardinality against 4096), and the
choice is made once at ingest for a workload that computes one pairwise operation. Two consequences
follow, and the second is ours:

1. When the universe is large, set mass spreads thinly enough that individual chunks stay under the
   cardinality threshold *even when global density is high*. The structure then runs array merges
   where a bitmap popcount would be correct. This is observable directly — a container census on a
   dense corpus shows [NUM: bitset container count, expected to be zero or near-zero] bitset
   containers at [NUM: global density]. **[TAB 3]**
2. There is no notion of amortizing the decision, because there is no batch. At N² scale the
   decision is made N² times, and whether it can be made cheaply becomes a first-order question
   rather than an implementation detail.

**The reframing.** Treat the choice as an object. Let each set be representable as a dense bitmap
(B), a sorted array (S), a run container (R), an EWAH-style fill-compressed word stream (W), or the
complement of any of these (C). The cross product is a **pairing matrix**: each cell is a distinct
kernel with a distinct cost function. B×S costs Θ(|S|). B×R with a rank index costs Θ(r), the number
of runs, *independent of how long the runs are*. S×S is a merge or a gallop. **Every cell except B×B
is sub-linear in the universe.** The algorithmic problem is no longer how fast a kernel runs; it is
which cell to enter, decided N² times, cheaply. **[FIG 1]**

That reframing raises the two questions the rest of the paper answers, and we pose them in our own
voice at the end of the Introduction: *given that popcount is solved and Roaring already dispatches
on container type, is there anything left for representation choice to buy — and if so, can the
choice itself be made cheaply enough at N² scale that the saving survives the deciding?*

---

**First answer: yes, and the shape of the win is not what the motivation predicted.** Sweeping both
sides through the full density range with universe pinned so cache residency is constant, B×B is
**flat** — the same cost per pair across [NUM: orders of magnitude of density spanned]. That is the
fixed-cost premise, measured rather than argued. Against that flat line the best pairing wins
[NUM: peak speedup at the sparse extreme] at the sparse end, and the advantage grows with the
universe. **[FIG 2]**

But the curve is **U-shaped**, and this corrects the project's own motivating framing. A row that is
99.99% ones is exactly as compressible as one that is 0.01% ones — its complement is a single run.
The operative variable is not sparsity; it is **distance from density ½**. The dense tail is won by
run- and complement-based pairings, the sparse tail by array- and rank-based ones, and between them
sits a band where B×B is genuinely the right kernel and nothing beats it. **Reporting that middle
band as a loss would be a mistake: it is the pairing matrix working.** The honest claim is not "we
beat bitmaps," it is "we identify, cheaply, the [NUM: fraction of the density range] where bitmaps
are wrong."

> **[GAP 1] The dense tail is the weak half of the U.** In the retired campaign it was won largely
> by R×R *incidentally* — the complement representation was built afterwards as a deliberate
> strategy and beat the accident by only about a fifth. A U-shape whose two arms have very different
> evidential standing is a reviewer's first target. The final campaign should sweep the dense tail
> as deliberately as the sparse one, with C×B and C×C as first-class competitors, so both arms rest
> on a designed strategy rather than one designed and one lucky.

**Second answer, and the paper's most transferable finding: the win comes from avoiding work, not
from accelerating it.** Across every asymmetric cell and every corpus shape, the kernels that win
are the ones that *skip*: a zone map that proves two rows disjoint at 1/512 of the cost of
discovering it directly, a rank index that answers an entire run with two lookups regardless of its
length, a galloping search that steps over the bulk of the dense side. Against these, throughput
optimizations on the same cells win [NUM: count] of [NUM: total] measurement points.

The reason is structural rather than incidental, and it is what makes the finding transfer. SIMD
accelerates work you have decided to do. In this problem, on skewed data, most of the work should
not be done at all. Vectorizing the asymmetric cells optimizes the part of the computation that is
already proportional to the small side — there is little there to accelerate — while the
irreducible work sits in B×B, which is the one cell where SIMD does pay and where the field has
already spent its effort. **Faster popcount is a constant factor on one cell; representation
selection is an asymptotic factor on the rest.** **[FIG 3a]**

**And the two strategies scale in opposite directions with the memory hierarchy.** As the working
set grows from L2-resident to DRAM-resident, vectorization's advantage decays on every
microarchitecture measured, while work avoidance's is maintained or grows. **The gap widens
everywhere.** This is the forward-looking part of the result: the trend line favours avoiding work,
and it favours it more on every generation that adds cores faster than it adds bandwidth. **[FIG 3b]**

> **[GAP 2] The "opposite directions" claim is half ARM-only.** Last time, the decay of SIMD
> replicated on all three hosts but the *growth* of work avoidance did not — it was flat on x86.
> The claim had to be weakened to "the gap widens everywhere," which is true but much less
> quotable. Worth designing the final residency sweep to settle this properly: more residency
> points, both spectra, all four hosts, and an explicit DRAM-resident regime rather than stopping
> at L2.

> **[GAP 3] The winning-strategy map has no display item.** The claim that skipping beats
> throughput is currently carried by a count in prose. It deserves a figure of its own: a cell ×
> corpus-shape matrix, each entry coloured by which *strategy class* wins there — rank lookup, zone
> map, search strategy, throughput. Read positively, that figure is a map of *which mechanism to
> reach for where*, which is more useful to a reader than any ratio, and it is cheap to produce from
> data the campaign already generates.

---

**Third answer: the deciding nearly eats the saving, and only one way of deciding survives.**
This is where the project's own falsifiable claim failed and had to be re-engineered, and the paper
is stronger for saying so plainly.

Selecting per pair from precomputed O(1) metadata — cardinality, run count, chunk occupancy — costs
[NUM: per-pair selection as % of runtime] of runtime against a 2% budget. It fails. The saving is
real and the decision consumes it.

Hoisting the decision to tile granularity — partition rows by density and run count so a whole tile
of pairs shares one kernel choice — brings it to [NUM: per-tile selection cost as %], comfortably
inside budget. But hoisting alone exposed a defect that cost had been masking: on at least one host
the tile model's *choice* was worse than making no choice at all.

The fix is the finding. Replace the model with a measurement: time a handful of a tile's pairs under
the candidate kernels, commit to the winner for the rest of the tile. **Modelled selection is worse
than no selection; measured selection is better than both.** Regret against a bucket oracle ranks
them [NUM: probe regret] < [NUM: per-tile regret] < [NUM: all-bitmap regret] < [NUM: per-pair model
regret]. **[FIG 4]**

This is the ε→0 special case of Micro Adaptivity and must be cited as such. Its residual distance
from the oracle is a one-line scope statement in the limitations subsection, not a theme.

**A corollary that generalizes past this paper: the work-avoidance filter is itself a decision, not
a component.** Applying the zone map unconditionally is a *net loss* across the corpus set — it
helps enormously where rows are disjoint and is pure overhead where every bin is occupied. Gated on
a cheap test it never loses and often wins large. A filter only pays when it filters, and knowing
when it filters is the same selection problem one level down. **[FIG 5]**

---

**Fourth: does any of this survive contact with real data?** The synthetic sweeps establish
mechanism; they cannot establish that the mechanism is *reachable*, because a generator can
manufacture the structure its own kernels exploit. So the evaluation runs the corpora CRoaring's own
benchmark harness runs by default, plus larger modern graphs, against a CRoaring tuned the way a
competent user would tune it — `run_optimize()` and `shrink_to_fit()` before timing, which makes it
[NUM: CRoaring tuning speedup] faster than default and is what every ratio is computed against.

The headline is a ratio range, and it is the least interesting part. **The result is the
distribution of winners.** Across the corpus set the winning cell migrates with density: dense
corpora go to B×B, sparse ones to B×S and B×R, clustered ones to R×R, and the fill-compressed cell
loses almost everywhere. [NUM: winning-cell histogram] **Had one column dominated, the pairing
matrix would be unnecessary and this paper would have refuted itself.** **[TAB 1]**

> **[GAP 4] — the most serious one. The system is never measured as a system.** Every
> corpus-versus-CRoaring ratio uses the *best cell per corpus*: an oracle. Every selection-cost
> number comes from a different harness. The paper therefore asserts "the right cell beats
> CRoaring" and "choosing costs under 0.4%" and invites the reader to multiply them — which is
> exactly the composition the evidence does not contain. A reviewer will find this immediately and
> it is fatal if unaddressed.
>
> What defuses it: **one table where the deployed selector, running end to end on real corpora,
> beats tuned CRoaring** — no oracle, selection cost included, build costs charged. This is the
> single highest-value experiment in the final campaign, and it is worth reorganizing the campaign
> around. It also subsumes several smaller gaps, because an end-to-end harness forces the tile
> pipeline, the gate and the cell library to compose behind one entry point rather than existing as
> parallel benchmark binaries.

> **[GAP 5] Coverage is uneven between the synthetic and real evaluations.** Last time the real
> corpora ran on one microarchitecture while the four-host result was synthetic. A reviewer reads
> that as the real result being single-host. If the final campaign runs the real corpora on all four
> hosts, the paper gains its most defensible table for very little extra cost.

> **[GAP 6] Precision discipline.** The previous corpus ratios were not stable to two significant
> figures across repeats, and the winning cell itself changed on several corpora between runs. The
> final campaign needs repeats, medians, and a stated dispersion — and the paper should quote
> ranges and medians, never per-corpus point ratios. This is cheap and it converts the table's
> biggest weakness into evidence of rigour.

---

**Fifth: storage, reported even though speed is the objective.** Representations here are chosen for
intersection speed, not size, so storage is a reported trade-off rather than a goal. It earns its
place for two reasons. It states in the storage domain exactly what the timing tables show — a dense
bitmap over a large sparse universe is not a compression scheme but a liability, costing
[NUM: bits/value for sparse corpora as a dense bitmap] against [NUM: bits/value for the best
representation]. And it surfaces something no timing measurement could: **the selector's own
metadata — rank index and zone map — is proportional to the universe rather than to the data, and is
built unconditionally.** On the largest corpus that is [NUM: metadata-to-data ratio] more metadata
than data. This is a defect, not a design choice, it is disclosed as one, and it is invisible in
every throughput number because rows are built before the clock starts. **That is itself an argument
for reporting storage even when throughput is the objective.** **[TAB 2]**

---

**Sixth: where it does not pay.** Stated as its own subsection, at the end, opening concessively.
The mid-density band belongs to B×B and the method's job there is to get out of the way. Layout
determines regime: the same genomic data in variant-major orientation sits deep in the sparse regime
while in haplotype-major orientation it sits mid-band, where all-bitmap wins outright — real data
gives a large universe *or* sparsity far more often than both. Selection is cheap but not good.
The metadata defect above makes the current implementation undeployable on sparse large-universe
data regardless of speed. And the tile pipeline has been measured on block-diagonal pairs only.

> **[GAP 7] Cross-tile pairs and the transpose build.** Both are currently excluded — the first
> untested, the second by an amortisation argument. Together they mean no number in the paper is
> truly end-to-end. This is the same hole as [GAP 4] seen from the other side, and the same
> experiment closes both.

---

**The discussion.** Compress to two sentences, then say the things Results cannot. That the
contribution is a *composition*, and that this is the same shape as Roaring's own contribution —
array containers, bitset containers, run containers and SIMD intersection were each known, and what
landed was the system. That the boundary between work avoidance and work acceleration is the
transferable result, and that the memory-hierarchy trend moves it further toward avoidance every
generation — which is the forward-looking claim, and the one a reader takes to a different problem.
That heterogeneity, not sparsity, is the property that makes representation choice pay, which
predicts where else this applies. And a short, unapologetic statement of what remains: the selector
is cheap but not optimal, and the machinery is not yet reachable behind the C ABI.

---

## 4. Framing rules for the drafters

Roaring's Introduction ends by posing its two hardest objections as explicit questions in its own
voice, then structures its entire evidence section as the answer to those two sentences in order.
Steal the device exactly. Our two questions, to close the Introduction:

> Given that popcount is a solved problem and that Roaring already dispatches on container type per
> chunk, is there anything left for representation choice to buy? And if a wider representation
> matrix does buy something, can the choice itself be made cheaply enough at N² scale that the
> saving survives the deciding?

Results §1–2 answer the first. Results §3 answers the second. That is the whole architecture, and it
is why the ordering below is not negotiable.

**State the foil fairly, and only once.** Not "Roaring is slow" — Roaring is excellent, it is
deployed everywhere, and nobody here is going to out-engineer Lemire at his own data structure. The
foil is *fixed granularity*: a constant chunk width and a constant cardinality threshold, chosen
once at ingest, for a workload that computes one pairwise operation. Everything the paper claims
against Roaring follows from that one sentence, and any wording that reads as disparagement of the
implementation rather than of the fixed granularity is wrong and will read as defensive.

**Two devices worth copying from the Roaring paper.** It names its own worst case explicitly,
quantifies the penalty exactly, and then reframes rather than hiding it. And it declines to
manufacture a single champion number where the trade-off is genuinely data-dependent, reporting both
options as first-class. Both fit this paper: the mid-density band is our named worst case, and the
selection policies are a genuine data-dependent trade-off.

**Before the container-census claim is drafted:** it is flagged in `RESEARCH_PLAN.md` §16.1 as
requiring independent verification, and this narrative now leans on it for a whole Results
subsection. Verify against CRoaring `master` first. See [GAP 8].

**Two prior-art statements verified from source during this pass** — both tier 2 (sourced), citable
as statements about CRoaring v4.7.2 as vendored at `third_party/croaring_amalg`, and both usable in
the Introduction to establish the opening without relying on our own measurements:

1. The container threshold is a compile-time constant: `enum { DEFAULT_MAX_SIZE = 4096 };`
   (`roaring.h:2486`), with the adjacent comment "Containers with DEFAULT_MAX_SIZE or less integers
   should be arrays" and the dispatch at `roaring.h:5515`. The *mechanism* of fixed-threshold
   container choice is therefore established from source; only the empirical container census still
   needs the re-run.
2. `run_bitset_container_intersection_cardinality` (`roaring.c:10901`) iterates the runs and calls
   `bitset_lenrange_cardinality` per run — **cost proportional to run length**. This is precisely
   the cost a rank index removes, since `rank(b) − rank(a)` answers a run in constant time
   regardless of its length. It is the cleanest possible motivation for the B×R cell and it does not
   depend on any measurement of ours.

Both belong in the Introduction or in R5, stated as what the source says, attributed to the version
and file. Neither may be described as a deficiency of Roaring the *format* — they are properties of
a materialisation-oriented implementation being used for a cardinality-only query.

---

## 5. Claim ledger — what may appear, at what strength

> **Read this before using any number below.** The measurement campaign these figures come from has
> been retired and `results/` has been cleared; final runs are in flight. **Every numeric value in
> this section is an order-of-magnitude placeholder recording what the claim looked like last time,
> not a value to be typeset.** What this ledger fixes is the *shape* of each claim — what is being
> asserted, what the separating axis is, what would falsify it, and which display item carries it.
> Refill the numbers from the final campaign before drafting, and re-run the audit in §11.
>
> The distinction that matters: a claim whose shape is stable survives a re-measurement with new
> numbers in the same slots; a claim whose shape depends on a specific magnitude does not. The table
> immediately below sorts them.

### Claim stability under re-measurement

| Stable — shape survives, refill the number | Fragile — re-measurement could invalidate the claim itself |
|---|---|
| SIMD wins no asymmetric cell (the *count* may move; "zero or near-zero of N" is the claim) | Any specific per-corpus ratio quoted to two significant figures |
| Cost is U-shaped in density; the operative variable is distance from ½ | The exact crossover density |
| Per-pair selection fails a 2% budget; hoisted selection passes by an order of magnitude | The precise selection-cost percentages |
| The winning cell migrates across corpora and no single cell dominates | The exact winning-cell histogram |
| Measured selection beats modelled selection | The regret percentages, and whether anything reaches 1.5× of oracle |
| Vectorization's advantage decays with working-set size | Whether work avoidance *grows* rather than merely holds — this was ARM-only last time |
| A rank index removes run-length dependence from B×R | The crossover run length |
| Work avoidance must itself be gated or it is a net loss | The fraction of corpora harmed when ungated |
| A dense bitmap over a large sparse universe is a storage liability | The bits-per-value figures |

**If a stable-column claim does not reproduce in the final campaign, that is a finding, not a
setback — and it belongs in the paper.** This project's record is that three of six gap-closures
made the paper weaker and were kept, which is why its evidence is worth trusting.

Everything below is tier 3 (measured on target) unless marked. Nothing tier 1 appears anywhere in
the paper as fact.

> **Reframing note.** Every claim below was measured on **intersection**. Under the set-algebra
> framing each one now carries an implicit "for intersection" until the operation sweep of [GAP 11]
> lands. A1–A10 are still the claims; their scope is narrower than the thesis until that gap closes.
> **A11 is new and unmeasured**, and it is the claim the reframing rests on.

### Admissible, load-bearing

| ID | Claim | Number | Source |
|---|---|---|---|
| **A11** | The cost map holds across the boolean operation set — union, intersection, difference, symmetric difference — with the winning pairing varying by operation as well as by density | **UNMEASURED** — see [GAP 11]. This is now the paper's widest claim and it has no evidence yet | — |
| **A1** | SIMD wins no asymmetric cell | **0 of 25** corpus-cell points | `results/OPTLOG.md` |
| **A2** | **Under the i.i.d. null model** the expected costs of $\rS,\rC,\rR,\rW$ are monotone in effective sparsity $s=\min(d,1-d)$ and lie within constant factors of one another, so the cost curve is U-shaped and symmetric about $d=\tfrac12$. **This is a statement about the null model and about $\rB$'s flatness — not a cost law for any representation on real data**, where structure at fixed density moves $\rR$ and $\rW$ by unbounded factors (that deviation is the contribution). The measured quantities are the crossover in $s$ and the size of the win | crossovers are universal constants: $\rR$ at $d\approx0.0159/0.9841$, $\rS,\rC$ at $1/w=0.0156$ and its complement | `figures/plot_fig2_expectations.py` (analytic) |
| **A3** | **RESTATED — see [GAP 14].** Against a *competently tuned* CRoaring the kernel-level dense-corpus advantage is withdrawn; the sparse-corpus advantage is untouched, and **with the meta-filter layer on top Storm still wins**. Report both baselines | **17 of 17**, 1.12–16.06×, median ≈5.4× | `results/CORPORA.md` |
| **A4** | The winning cell migrates with density | B×B ×10, B×S ×3, B×R ×2, S×S ×1, R×R ×1; W×W below 1.0× on 16 of 17 | `results/CORPORA.md` |
| **A5** | Selection is near-free only when hoisted | per-pair 27.7–48.4% (fails); per-tile 0.10–0.29%; probe-and-commit 0.30–0.48%; 0.03–0.37% on real corpora | `results/OPTLOG.md`, §23 |
| **A6** | Measured selection beats modelled selection | probe regret 59.0% vs per-tile 106.7%, all-bitmap 118.0%, per-pair model **403.7%** — the model alone is worse than no selection | F15 |
| **A7** | A rank index makes B×R cost Θ(runs), independent of run length | run length varied 256× at fixed run count: unindexed grows 18×, indexed shows no trend; crossover ~256-bit runs | `bench/p4_runlength.sh` |
| **A8** | Vectorization's advantage decays with working-set size while work avoidance holds or grows | SIMD 1.52→1.17 (M4), 1.56→1.03 (SVE2), 5.38→1.55 (SPR); avoidance 8.8→20.5, 12.4→22.8, 15.3→14.8 | F17 |
| **A9** | Work avoidance must itself be gated | the **width-parameterised overlap pre-filter** applied unconditionally is a net loss (geomean 0.763×, worst 0.09×, 11 of 25 corpora harmed); gated, geomean 1.816×, worst 1.00×, none harmed | §20 (C29), `bench/filter_ablation.sh` |

> **Three distinct filters, and an earlier version of this ledger conflated two of them.** Keep them
> separate in the prose, name which one every number belongs to, and do not let a claim about one
> migrate to another.
>
> | Mechanism | What it is | Where measured | In Figure 1? |
> |---|---|---|---|
> | **Zone map** | One bit per 512-bit bin of a row, built by `build_row()`. `occ_A ∧ occ_B` popcount is the exact bin count to visit; zero proves disjointness | `bench/ablate_zonemap.py`; `occ_overlap()` in `kernels/storm_repr.h` | **yes — panel c** |
> | **Overlap pre-filter** | The same occupancy idea at a *chosen* width (default 2²⁰ bits), applied as a tile-level pre-filter ahead of the B×S pipeline. This is what A9's 0.763×/1.816× numbers measure | `bench/filter_ablation.sh` (C29) | **yes — panel d, the gating bars** |
> | **Prefix filtering** | Jaccard/similarity-join prefix pruning in the **opt-in thresholded mode**. A candidate-pruning technique from the similarity-join literature (Bayardo et al.), not a representation or a summary | `bench/bench_threshold.cpp`, `sweep_threshold.sh` | **no — and it should stay out** |
>
> Prefix filtering belongs to a mode that changes the contract (only pairs above a threshold are
> reported) rather than to the exact set-algebra default. Putting it in the overview figure would
> imply it is part of the core design. Keep it to at most a short subsection or Supplementary, and
> cite Bayardo/Xiao when it appears.
| **A10** | Storage: a bitmap over a large sparse universe is a liability, not a compression scheme | dense corpora 3–7 bits/value; sparse corpora 10³–10⁶ bits/value as a dense bitmap | §24 |

### Admissible, secondary

| ID | Claim | Number |
|---|---|---|
| **B1** | Cross-ISA vs tuned CRoaring on synthetic skew | 1.7–19.1× across four hosts; peak 19.1× on Sapphire Rapids |
| **B2** | vs all-bitmap on real corpora, corrected baseline | UShER 146.5×, gnomAD 278.2×, graphs 172–1083×, dense 1.5–4× |
| **B3** | Complement representation makes the dense tail a deliberate strategy rather than an accident | see [GAP 1] — the final campaign should make this a designed arm of the U, at which point it is a primary claim, not a secondary one |

**Cut from the paper** (blog post, or one unread Supplementary table): Harley-Seal, D2
run-collapsing, register blocking, prefetch behaviour, hashed Bloom filters, dual-tree row
hierarchies, the ~180 per-cell losing variants, and the port-pressure investigation. None of them
justifies a design decision that survives into the final system, which is the only test that
matters.

### Must be stated as limitations, in main text

| ID | Limitation |
|---|---|
| **L1** | Selection is not solved, only no longer harmful — nothing is within 1.5× of the oracle |
| **L2** | Human variant data gives a large universe *or* sparsity, rarely both. Haplotype-major 1KGP3 chr20 measures **0.93×** — all-bitmap wins. Variant-major gnomAD/UShER land at 147–278×. The layout determines the regime and the paper must say so |
| **L3** | Selector metadata is universe-proportional and unguarded: 4 MB of metadata attached to 155 bytes of data on `enwiki-categorylinks` (26,312×). A known, unfixed defect |
| **L4** | No threading; single-threaded throughout. Out of scope by construction |
| **L5** | Real-corpus run is one microarchitecture (Apple M4). The four-host result is synthetic |
| **L6** | **Precision.** `CORPORA.md` ratios are not trustworthy to two significant figures — repeats span 16.1–18.2× on one corpus and 1.2–3.7× on another, the winning cell itself changes on 5 of 17 between repeats, and the file documents an uncorrected pessimistic bias from thermal and cache-pollution accumulation across the back-to-back batch. `uscensus2000` and `dimension_033` are flagged by the file as unquotable until the harness is fixed. **Report ranges and medians, never two-significant-figure per-corpus ratios, and drop those two corpora from any headline** |
| **L7** | Every CRoaring and cross-ISA ratio uses the **best cell per corpus — an oracle**, not the shipped selector. Selection cost is measured separately. The paper must never present an oracle number and a selection-cost number as if they compose into an end-to-end result |
| **L8** | The tile pipeline measures **within-tile (block-diagonal) pairs only**; cross-tile blocks are untested and the transpose-build cost is excluded by an amortisation argument |

### Excluded

**Any claim of an SVE or SVE2 kernel.** No SVE/SVE2-specific intrinsic code exists in `kernels/`.
The Graviton3- and Graviton4-class hosts run the generic NEON path on SVE-capable hardware. The
honest statement is **four microarchitectures, two hand-written kernel paths (NEON and AVX-512
VPOPCNTDQ) plus a portable scalar fallback**. Likewise there is no AVX2 or SSE4.2 kernel in
`kernels/`; the `STORM_ENABLE_SIMD_AVX2`/`SSE4_2` CMake options gate only the legacy `libalgebra`
path and are not evidence of one.

The 10⁴×/10⁵× haplotype napkin figures (tier 1, never measured). The `VPOPCNTQ` port-5 assignment
and the derived 0.125 cycles/word ceiling *as an x86 claim* — tier 2 at best, citable only as a
published vendor figure with attribution, never as a statement about our kernel. Any Harley-Seal
"net loss" argument on AVX-512 (tier 1; the ARM measurement is tier 3 and may be stated, scoped to
ARM). RVV throughput of any kind — no published set-intersection kernel exists.

---

## 6. Section skeleton

Six Results subsections. Each delivers exactly one claim; each names its separating axis in its
opening sentence. Separating axes are listed to satisfy the disjointness test.

### Introduction (unheaded, 5–6 paragraphs)

Bitmaps and the all-pairs cardinality problem → why fixed-cost B×B is the universal default → the
skew that makes it wrong → Roaring as the state of the art *and* as a fixed-granularity commitment →
the two questions (§4) → one-paragraph roadmap, spent once, previewing the findings in order.

### Results

**Revised for the set-algebra framing.** Six subsections, one claim each, separating axis named in
each opening sentence.

| # | Heading | Claim | Separating axis vs. previous |
|---|---|---|---|
| **R1** | The cost of a set operation is U-shaped in density | A2 | — (opening) |
| **R2** | The cost map holds across the boolean operation set | **A11** | operation varied, density held |
| **R3** | Avoiding work beats accelerating it, and the gap widens with working-set size | A1, A8 | strategy compared, not data |
| **R4** | Selecting the pairing is cheap in both regimes | A5, A6, A9 | cost of deciding, not of computing |
| **R5** | The winning pairing migrates across real corpora and operations | A3, A4 | real data, not synthetic sweeps |
| **R6** | Fixed chunk granularity misprices dense corpora | C11 | competitor's mechanism, not ours |
| **R7** | Where the method does not pay | L1, L2, L3 | scope limits |

That is seven, one over budget. **Merge R6 into R5** unless the container census grows past a
paragraph: both are real-corpus evidence about why a fixed-granularity competitor loses, and R5's
closing paragraph is the natural home for the mechanism behind its own table. Keep them separate only
if the census carries its own table *and* its own claim.

**R2 is new and it is the subsection the reframing exists to support.** It varies the operation with
density held fixed, which is a clean separating axis, and its finding is either "the map is
operation-invariant" (simple, strong) or "the map depends on the operation" (richer, and a better
argument for a selector that knows the operation). It cannot currently be written — see [GAP 11].

**R4 replaces the old "selection must be hoisted" with a two-regime claim**, which the reframing
makes necessary and which is a better result. For a single operation between two large sets, the
decision is read from O(1) metadata and is negligible against the operation it governs. In a batch,
where each operation is small, the same decision is a large fraction of runtime and must be hoisted
to tile granularity and made by measurement rather than by model. **The cost of deciding scales with
how small the thing being decided about is** — that is the general statement, it covers both
regimes, and it keeps the tile-hoisting and probe-and-commit work fully load-bearing rather than
orphaned by the reframing.

R1–R4 establish the mechanism; R5–R6 test it on real data against a real competitor; R7 is the
concessive bridge into Discussion and opens with "Although…". Comparative material sits late by
design, and no subsection is headed by a competitor name or a bare metric.

**The escalation ladder relaxes one assumption per step:** controlled density, one operation (R1) →
controlled density, all operations (R2) → strategy comparison (R3) → cost of deciding charged (R4) →
real corpora (R5) → competitor mechanism (R6) → scope limits (R7).

### Discussion (no subheadings, 4–5 paragraphs)

Compress the thesis in two sentences, then pivot to material absent from Results: why work avoidance
and work acceleration scale oppositely with the memory hierarchy and what that predicts for future
cores; the relationship to Roaring stated generously and precisely; what the work-avoidance
principle transfers to beyond this problem; the artifact gap (§10) stated honestly; the follow-on.

### Methods (subheaded, ~2,500–3,000 words)

Representation definitions and cost model · the pairing matrix and its ten cells · zone map and rank
index construction · the selection policies (per-pair, per-tile, probe-and-commit) with exact
thresholds · corpora, provenance and loaders · benchmark protocol, cache-residency statement,
sampling and aggregation · correctness oracle and differential testing · hardware and toolchain ·
data and code availability.

Every equation, threshold and hyperparameter lives here and **nowhere else**. Results carries
headline numbers only.

---

## 7. Display items — six main, and what each must show

Full panel-by-panel specification, data schemas, captions, palette and build routes in
[`DISPLAY_ITEMS.md`](DISPLAY_ITEMS.md). The inventory below is the settled allocation.

| # | Item | Content | Serves |
|---|---|---|---|
| **Fig. 1** | Schematic | The pairing matrix: the cells, the cost function per cell, and the path from row metadata through the gate to a kernel. Native TikZ; `simdgrid.sty` for the representation lane strips | thesis, R1 |
| **Fig. 2** | Density sweep | Log-log, every cell, the flat B×B line, both arms of the U, both crossovers marked | A2 |
| **Fig. 3** | Strategy map + residency | (a) cell × corpus-shape matrix coloured by *which strategy class wins there*; (b) the advantage of each strategy class against working-set size, all hosts | A1, A8 |
| **Fig. 4** | The cost of deciding | Three panels: selection cost against the 2% budget by policy and host; regret against the bucket oracle; the gated-versus-ungated filter comparison | A5, A6, A9 |
| **Table 1** | Real corpora | Per corpus: universe, median density, winning cell, versus tuned CRoaring, versus all-bitmap. Ratios as median [IQR] over interleaved repeats, never point values | A3, A4 |
| **Table 2** | Container census | Array / bitset / run container counts per corpus after `run_optimize()`, against global density — the mechanism behind fixed-granularity mispricing | R5 / C11 |

**Three changes from the first draft of this inventory, all of them right.** The container census was
promoted to a main table: it is the paper's most concrete mechanistic evidence and R5 is a whole
subsection built on it, yet it previously had nothing to point at. Storage moved to Supplementary,
which follows directly from the project's own scope rule that representations are chosen for speed
and storage is a reported trade-off rather than an objective — the claims stay in prose, they just
do not need main-text axis space. And filter gating folded into Fig. 4 as a third panel rather than
consuming a seventh slot, because cost, regret and gating are one coherent question about the
correctness and price of deciding.

Fig. 1 is non-negotiable — a reader who cannot picture the pairing matrix cannot read anything else.
Fig. 3a is the paper's most reusable single artefact: a map of which mechanism to reach for where.
Table 1 is the result, and the winning-cell column carries it more than any ratio in the table.

**Supplementary (S1…):** storage and bits-per-value, including the selector-metadata disclosure; the
four-host synthetic CRoaring comparison; B×R run-length isolation; correctness-testing record;
per-corpus raw timings with dispersion. The per-cell optimization record goes in a blog post.

**Abstract audit.** Every abstract clause must trace to a main item: the work-avoidance claim →
Fig. 3a; the corpus sweep → Table 1; the selection cost → Fig. 4; the U-shape → Fig. 2; the
fixed-granularity mechanism → Table 2. No clause about storage or about genomics enters the abstract.

**Two defects in the plotting code, to fix before any figure regenerates.** `bench/plot_paper.py`
labels two of the ARM hosts as SVE and SVE2, which contradicts the fact that no SVE-specific kernel
exists — they run the generic NEON path, and the labels must say so. And Fig. 3's residency data is
currently hand-transcribed Python literals rather than read from a results file; it needs a real
schema and a sweep script before it can be trusted or regenerated.

---

## 8. Citations — the ones that must appear, and where

**There are three distinct Roaring papers, and an earlier version of this section conflated two of
them.** They are routinely confused in the literature; get them right, keep three separate bib keys,
and take exact volume/issue/page metadata from the verified `references.bib`, not from here:

| Key | Authors | Title | Venue (verified) |
|---|---|---|---|
| `chambi2016roaring` | Chambi, Lemire, Kaser, Godin | Better Bitmap Performance with Roaring Bitmaps | SPE **46(5):709–719**, 2016 |
| `lemire2016roaring` | Lemire, Ssi-Yan-Kai, Kaser | Consistently Faster and Smaller Compressed Bitmaps with Roaring | SPE **46(11):1547–1569**, 2016 (arXiv:1603.06549) |
| `lemire2018roaring` | Lemire, Kaser, Kurz, Deri, O'Hara, Saint-Jacques, Ssi-Yan-Kai | Roaring Bitmaps: Implementation of an Optimized Software Library | SPE, 2018 |

The volume and page range this document previously gave for `chambi2016roaring` was in fact
`lemire2016roaring`'s. Both are now verified against DBLP and the publisher; `references.bib` is
authoritative and this table is a convenience copy.

**`census1881`'s published corpus statistics come from `lemire2016roaring`** (arXiv:1603.06549),
not from `chambi2016roaring` — that is the paper reporting the corpus, and it is the correct
citation for the loader-validation check in Methods.

`lemire2016roaring` is the run-container paper — the one whose narrative anatomy this document is
modelled on, and the right citation for run containers and for the compression-then-speed evidence
ordering. `lemire2018roaring` is the engineering-and-evaluation paper and the one that carries the
venue-fit argument in §1. `chambi2016roaring` is the original two-level container structure.

**Introduction, prominently and first:** all three Roaring papers as appropriate · Muła, Kurz &
Lemire, *Computer Journal* 61(1):111–120, 2018 (owns AND+popcount; the paper must not appear to
re-claim it) · BitFunnel (Goodwin et al., SIGIR 2017 — cite **first and up front**, not buried in
related work, since zone maps are functionally identical to `occ_A AND occ_B`).

**Selection and adaptivity:** Micro Adaptivity in Vectorwise (Răducanu, Boncz, Żukowski, SIGMOD
2013) — probe-and-commit is the ε→0 special case and **must be cited by name** · EmptyHeaded
(Aberger et al., SIGMOD 2016 / TODS 2017) · IA-SpGEMM (Niu et al., ICS 2019) · OSKI (Vuduc et al.
2005).

**Rank/select:** Jacobson (FOCS 1989) · Clark (1996) · Vigna, rank9 (WEA 2008) · Zhou, Andersen &
Kaminsky, Poppy (SEA 2013).

**All-pairs / prior tiling:** Bayardo, Ma & Srikant (WWW 2007) · Xiao et al. (WWW 2008) · Haque,
Pande & Walters, *JCIM* 51:2345–2351 (2011) · Dalke, chemfp, *J. Cheminformatics* 11:76 (2019) ·
Swamidass & Baldi, *JCIM* 47:302–317 (2007) — the pruning bound we deliberately do not implement.

**Compressed bitmap lineage:** WAH, Concise, EWAH as the RLE family Roaring displaced.

**Domain anchor:** Fu, *Theor. Pop. Biol.* 48(2), 1995 for E[ξᵢ] ∝ θ/i · PLINK (Purcell et al. 2007;
Chang et al., *GigaScience* 2015) · Tomahawk, by the same author, cited as prior art and as the
application, not as a competitor.

`references.bib` has 37 entries and **zero** bitmap-compression literature. Essentially all of the
above must be added. The existing entries belong to the unrelated paper the LaTeX scaffold was
copied from and are not a starting point.

---

## 9. What the code actually is — for Methods

Verified by building and running the test suites, not by reading documentation.

**Representation layer** (`kernels/storm_repr.{h,cpp}`): five views — `BitmapView` (B, with optional
rank9-style prefix-popcount index and a 512-bit-bin zone map), `ListView` (S), `RunView` (R),
`EwahView` (W, EWAH-64), `ComplementView` (C).

**Eleven pairing cells**, each with multiple competing variants raced against a shared scalar oracle
(`kernels/oracle.cpp`): B×B, B×S, B×R, B×W, S×S, S×R, S×W, R×R, R×W, W×W, C×B, in `cell_bb.cpp`,
`cell_bs.cpp`, `cell_br.cpp`, `cell_sparse.cpp`, `cell_wah.cpp`, `cell_comp.cpp`. There is no
Roaring meta-cell; Roaring is the external baseline only (`third_party/croaring_amalg`, v4.7.2).
Note that the paper's "ten cells" language refers to the ten non-Roaring cells of the symmetric
matrix; C×B is the eleventh and is a strategy, not a symmetric cell. Be consistent about which count
is used and define it once.

**Correctness:** `test_storm` 1,279 checks, 0 failures; `test_cells` **2,113,377** checks, 0
failures, differential against the oracle; 2.4–2.7M differential checks per host family in the
cross-ISA campaign, 0 failures. `nm` confirms 42 unmangled `STORM_` exports and zero defined mangled
symbols.

**Corpus generation** (`kernels/storm_gen.h`) implements the two required independent axes:
within-row `Structure{Uniform, Clustered, Runs}` and across-row `Spectrum{Uniform, Inverse(1/i),
Bimodal}`. Both spectra are exercised. Note that `results/density.jsonl` (1,260 rows) is
**uniform/uniform only** — the 1/i data lives in the raw iteration logs.

**Cache residency** is stated with every OPTLOG throughput number (Apple M4, L1d 64 kB, L2 4 MB
P-cluster, SLC 16 MB) and is itself the subject of A8.

**Legacy `storm.h`/`storm.cpp`** is the 2019-era API backed by vendored `libalgebra`. It is a
separate, older code path, it runs scalar fallbacks on AArch64, and it is **not** the vehicle for any
claim in this paper. Do not describe it in Methods except to say the pairing-matrix work is separate
from it.

---

## 10. The honest problem, and how the paper handles it

`RESEARCH_PLAN.md` §16.3 states plainly: every result from C11 onward lives in `bench/`; `storm.h`
still exposes the 2019 API; the pairing matrix, the gate, the tile pipeline and the thresholded mode
are none of them reachable through the C ABI. **There is currently no artifact.** §24 adds that the
unguarded selector metadata makes the implementation undeployable on sparse large-universe data
regardless of speed, and that this blocks the ABI work.

This matters because the model being followed — Roaring — landed on composition *plus packaging*,
and we currently have the composition as a set of parallel benchmark binaries.

Two options, and the choice must be made before drafting, not after:

- **(a) Publish as an empirical study.** Legitimate, honest, and the evidence fully supports it. The
  paper claims the composition and the map, describes the system, and does not claim a library.
  Requires no new code. Weaker impact; risks the outcome §16.6 names — the 2019 pattern of four
  dormant repositories.
- **(b) Close the ABI gap first.** Fix the C33 metadata guard, wire the selector into
  `storm_allpairs()`, produce end-to-end numbers with build costs charged, then write. This is the
  Roaring-shaped outcome and it is what §16 argues for. It also *changes the headline numbers*,
  because today's strongest figures exclude the transpose build by an amortisation argument and
  measure only the block diagonal.

**Recommendation: (a), with the artifact work sequenced immediately after and the paper's language
chosen so it does not have to be retracted when (b) lands.** The measurement campaign is finished
and internally consistent; the ABI work is a known quantity but it is weeks, and every day the
evidence sits unwritten is a day it drifts. Write the study now, describe the system as a system,
avoid the words "library" and "we release" in favour of what is true, and let the artifact follow.

If the author prefers (b), the narrative above survives unchanged — only the framing of §2's first
sub-thesis and the Discussion's artifact paragraph move.

---

## 11. Reconciliations the drafters must respect

1. **Never quote a single headline speedup.** The range spans 0.93× (haplotype-major chr20, where
   all-bitmap correctly wins) to four figures on sparse graph corpora. Always attach the regime.
2. **§22 (C31) is retracted by §23 (C32).** The "do not filter" conclusion and the 5.67×/1.29×
   contrast do not exist. What survives is that measured selection beats modelled selection by 2.17×
   on the bimodal corpus and 1.53× on the homogeneous one.
3. **Improving our own dense kernel shrank our own headline.** Adding AVX-512 took B×B from 742.1 to
   73.3 ns/pair, so the "vs all-bitmap" column *fell*. State this explicitly — it is a
   methodological point in our favour, and the CRoaring column is the honest headline, not the
   all-bitmap column.
4. **Genomics is not the motivating application.** It is the clearest natural example of a corpus
   where no single representation is right. 97.4% of gnomAD chr21 sites sit below AF 10⁻³ while
   0.75% carry 95.7% of the allele-frequency mass. Say that; do not say the method was built for
   genomics.
5. **Characterise every corpus by its median density, never its mean.** The distributions here are
   strongly skewed and the two differ by more than an order of magnitude on real data. This is a
   reporting rule for the paper, not a story about the project's history — the correction record
   stays out of the manuscript.

---

## 12. Gap register — what the final campaign must produce

These fell out of writing §2b end to end. They were not visible in the claim ledger, because a
ledger lets each claim stand alone and a narrative does not. Ordered by how much the story depends
on them.

| # | Gap | What closes it | New code? | Unlocks |
|---|---|---|---|---|
| **4** | **The system is never measured as a system.** Every CRoaring ratio uses a best-cell oracle; every selection-cost number comes from a separate harness. The paper asserts both and invites the reader to multiply them | One table: deployed selector, end to end, real corpora, selection cost included, build costs charged, versus tuned CRoaring | **Yes** — requires the cells, the gate and the tile pipeline to compose behind one entry point | The headline claim, and it subsumes gaps 7 and part of 6 |
| **1** | **The dense arm of the U is weaker than the sparse arm** — previously won incidentally by R×R rather than by a designed strategy | Sweep the dense tail deliberately with C×B and C×C as first-class competitors, mirroring the sparse sweep | No, the complement cell exists | Makes "distance from ½, not sparsity" a symmetric, designed result rather than half an observation |
| **5** | Real corpora measured on one microarchitecture while the four-host result is synthetic | Run the real-corpus set on all four hosts | No | The paper's most defensible table, for very little cost |
| **6** | Corpus ratios not stable to two significant figures; winning cell changed between repeats on several corpora | Repeats, medians, stated dispersion; quote ranges not point ratios | No | Converts the table's biggest weakness into evidence of rigour |
| **3** | No display item for which strategy class wins where | Emit strategy-class labels alongside per-cell timings | Trivial | **[FIG 3a]** as a map rather than a count |
| **2** | The residency claim's "widening gap" was ARM-only on the growth side | More residency points, both spectra, all four hosts, an explicit DRAM-resident regime | No | The forward-looking claim in the Discussion |
| **7** | Cross-tile pairs untested; transpose build excluded by amortisation | Same experiment as gap 4 | With gap 4 | Makes any number in the paper genuinely end-to-end |

Three further gaps came out of an adversarial read of the narrative against the repository, and they
are independent of the seven above. Full argument in [`GAPS.md`](GAPS.md).

| # | Gap | What closes it | New code? | Unlocks |
|---|---|---|---|---|
| **8** | **The sharpest single piece of evidence has no display item.** The container census — showing that a fixed cardinality threshold leaves a dense corpus with essentially no bitset containers — is the paper's most concrete, mechanistic finding about why fixed granularity misprices, and it appears nowhere in the six main items | Promote it to a main table **[TAB 3]**, and re-verify it against CRoaring `master` first, since it is currently flagged as needing independent verification | No | Results subsection R5, which currently promises a mechanism it cannot display |
| **9** | **One baseline of the six the project specified.** Only CRoaring is implemented. A hash-set intersection and a plain sorted-merge floor are cheap and answer "is this just a good merge?"; GraphBLAS was flagged as necessary by the project's own literature review and is absent rather than deferred with a reason | Add the two cheap floors; either add GraphBLAS or state explicitly why it is out of scope | Some, but small | Removes the most obvious reviewer question about whether the comparison set is chosen favourably |
| **10** | **Genomics is over-represented relative to its role.** It occupies five sections of the research plan and the evidence base leans on it, in a paper whose own scope rules forbid an application focus. A reviewer can read it as an application paper in kernel clothing | Demote genomics to one corpus family among several, and make sure graphs, IR and census data carry equal weight in Table 1 and in the prose | No — a writing and emphasis decision | Protects the domain-agnostic framing that the whole positioning depends on |

**Two open items now gate the campaign, not one.** [GAP 12] — reconciling the analytic model with
the measurements — joined [GAP 4] on 2026-08-05, and it is cheaper to close: one instrumented run
against existing binaries, versus new code for the end-to-end harness. Close 12 first.

**Gap 4 is the one to design the campaign around.** It is the only one that is fatal if left open,
it is one of only two that needs real new code, and closing it happens to force the components to
compose — which is separately the thing standing between this work and a usable artifact.

**Gaps 8 and 5 are the best value for effort.** Promoting the container census costs a table and a
verification run and it upgrades a Results subsection from assertion to evidence. Running the real
corpora on all four hosts costs machine time and nothing else. There are also several corpora
already fetched but never benchmarked; extending the comparison to them is close to free and widens
the universe range the paper can claim.

Everything else on both lists is a matter of running more of what already runs.

### [GAP 11] The operation set — opened by the reframing, and now the largest gap

Retiring the all-pairs framing widened the object from intersection cardinality to the whole
algebra. **Every measurement the project has ever taken is on intersection.** Union, difference and
symmetric difference are unmeasured, and the paper's central claim is now explicitly that the map
holds *across operations*.

This is the campaign's new top priority alongside [GAP 4], and it is more tractable than it sounds,
because the cells already exist — what is missing is the operation dimension over them.

| Needed | Why it matters |
|---|---|
| The density sweep repeated for union, difference and symmetric difference | Establishes whether the U-shape is a property of intersection or of set algebra. This is the claim the reframing rests on |
| Per-operation winning-cell maps | The pairing map may *differ* by operation — and if it does, that is a better result than if it does not, because it means the selector must know the operation, which no fixed-representation library can express |
| Real-corpus comparison against CRoaring for each operation | CRoaring implements all of these; the head-to-head becomes an operation × corpus grid rather than a corpus list |
| Which operations admit cardinality-only shortcuts | The rank-index identity answers a run in constant time for intersection cardinality; state precisely which operations inherit it and which do not |

**The honest fallback if the campaign cannot cover the full algebra in time.** Present intersection
as fully measured, union and difference as measured on a reduced grid, and state the scope plainly
in the limitations subsection. What is *not* acceptable is claiming the algebra and measuring one
operation — that is the same error the project's own record shows four times, generalizing from a
single measurement.

**A prediction worth stating and testing:** the asymmetry that makes selection pay is sharper for
intersection than for union, because an empty operand makes intersection trivial but leaves union
proportional to the other side. If that holds, the map is operation-dependent in a way that is easy
to explain and strengthens the case for a selector that knows the operation. If it does not hold,
the uniformity is itself the cleaner result. Either outcome is publishable; not measuring is not.

### [GAP 14] C35 — the CRoaring baseline was under-tuned, and it invalidates the dense-corpus claims

**Status: BLOCKING for Table 1 and for every "vs. CRoaring" number in the manuscript. Landed
upstream 2026-08-05 as `0b71ffb`, after the tables in this draft were populated.**

A twenty-line change to CRoaring — after `run_optimize()`, promote array containers above
cardinality 64 to bitsets, rather than leaving CRoaring's `DEFAULT_MAX_SIZE = 4096` — removes most of
Storm's dense-corpus advantage:

| corpus | Storm vs stock CRoaring | **Storm vs modified** |
|---|---:|---:|
| `census1881` | 13.40× | **0.59×** (a 1.7× loss) |
| `census-income` | 10.65× | **1.88×** |
| `wikileaks-noquotes` | 5.15× | 3.67× |
| `as-skitter` | 3.35× | 3.23× |
| `dbpedia-link` | 2.44× | **2.79×** |
| `uscensus2000` | 2.27× | **2.76×** |

**What this does to the paper.** The headline "beats tuned CRoaring on 17 of 17 corpora, 1.12–16.06×"
is **not defensible as written**. Standing rule 9 requires baselines tuned in good faith, and
`run_optimize()` alone no longer clears that bar once a better configuration is known and costs
twenty lines. Table 1's `census1881` (16.06×) and `census-income` (13.11×) cells are the worst
affected. Every `\prov{}` CRoaring value in the draft predates this.

**Why 4096 is the wrong constant, and why that matters more than the damage.** 4096 is where
`sizeof(array) = sizeof(bitset)` — a *memory* crossover. For count-only AND the compute crossover is
`k = 64`–`128` on this host, so the stock threshold is 30–60× off for this workload. CRoaring's
threshold is optimal for the objective it was chosen for and wrong for ours.

**What survives, and it is the thesis rather than a rescue.** The advantage C35 removes is a
*kernel-level* one — Storm's dense cells beating CRoaring's dense cells — and that was never the
contribution. **With the meta-filter layer applied on top (zone maps, gating, size bounds), Storm
still wins against the fixed CRoaring.** That is the cleanest possible demonstration of the paper's
own argument: repair the incumbent's kernel and the advantage persists, *because the advantage was
never kernel speed*. Avoidance sits above whichever kernel is underneath it, and it composes with a
better one just as well as with a worse one.

This is a stronger claim than the one it replaces, and it should be stated in exactly that order:
first that the naive dense-corpus win was a baseline artifact and is withdrawn; then that the layered
result stands against the repaired baseline. A reader who sees the second without the first will not
believe either.

> **The finding underneath is better than the one it damages, and the paper should lead with it.**
> The fix is *itself density-dependent* — 19.5× on dense corpora, 0.6–0.8× on sparse. **Neither 4096
> nor 64 is right, because no single fixed container threshold can be right at both ends of the
> density axis.** That is this paper's thesis demonstrated *inside the incumbent*, and it reframes the
> container-census argument from "Roaring gets this wrong" to "**Roaring needs adaptive selection for
> the same reason Storm does**" — which is a stronger, more generous and more interesting claim than
> the one it replaces. It is also directly contributable upstream.

**A hard constraint on step 2, verified in the source and easy to get wrong.** The threshold cannot
simply be lowered upstream: CRoaring's *portable serialization format* infers container type from
cardinality — `isbitmap = (thiscard > DEFAULT_MAX_SIZE)` at `roaring.c:14421` and `14553`, with only
a run-override bitmap beside it and no way to mark "bitset despite low cardinality". A promoted
container therefore writes as a bitset and reads back as an array: **it does not round-trip**, and
the validator rejects it. **4096 is a format constant, not a tuning knob.**

Two consequences the paper must state rather than discover in review. The promotion is necessarily
**in-memory only** — which is exactly the shape the existing patch already has, but that is currently
an accident of implementation rather than a documented constraint. And the upstream proposal cannot
be "change the constant"; it has to be an opt-in, non-persisted retuning step alongside
`run_optimize()`, or a format revision, which is a much larger ask. Say so plainly: it makes the
proposal credible rather than naive.

**The three-step arc this creates, and it is the paper's strongest sequence.** Do not present C35 as
damage control. Present it in order:

1. **The analysis predicts that a fixed container threshold cannot be right across the density
   axis** — the same argument the whole paper makes, applied to the incumbent.
2. **It is wrong, demonstrably, and the fix is twenty lines.** Promoting arrays above cardinality~64
   to bitsets after `run_optimize()` gains 19.5× on dense corpora — and *loses* 0.6–0.8× on sparse
   ones, so neither constant is right and the incumbent needs adaptive selection for exactly the
   reason this paper argues. This is a contributable upstream improvement, offered rather than
   scored.
3. **The avoidance layer still wins on top of the repaired baseline.** Zone maps, gating and the size
   bound sit above whichever kernel is underneath, so fixing the kernel does not remove them.

That sequence converts the paper's most awkward result into its most persuasive one. It also makes
the contribution unmistakable: **not "our kernels are faster", but "the avoidance layer wins
regardless of how good the kernel beneath it is"** — which is the thesis, tested against the
strongest version of the incumbent rather than the convenient one.

**What the campaign must do.** Report **both** baselines: stock, which is what a user gets today, and
modified, which is what the incumbent can do with a known fix. Re-derive every CRoaring column
against the modified baseline. **Sparse-corpus results are untouched and several improve**, so the
regime where the paper claims its contribution is unaffected — say that plainly rather than letting
it look like a rescue.

**Two further upstream changes also postdate the draft's numbers** and must be folded in: `C36`
charges $\cell{\rB}{\rB}$ for the zone-map scan it cannot avoid, and Table 3 re-run in sparse-only
mode moves `enwiki-categorylinks` from $0.99\times$ to $323.6\times$.

### [GAP 13] The reframing worklist — four sites that are structural, not editorial

From the architecture audit's file-and-line inventory ([`AUDIT.md`](AUDIT.md) §9). Most of the 28
committed sites are wording. **Four are not, and each needs a decision before the rewrite pass.**

**1. The Introduction's motivation has no replacement.** `introduction.tex:5–6` motivates the entire
paper with an N² amortisation argument — the aggregate cost across `N²` operations, not the cost of
any one. Under two-arbitrary-sets framing there is no aggregate. This paragraph cannot be edited; it
needs a new motivation. The obvious candidate is the one the theory now supplies: *a fixed-cost
kernel provably leaves headroom on the table at every density away from ½, and the gap widens
without bound* — which motivates a single operation as well as a batch.

**2. R3's headline finding is a special case under the new frame.** "Selection must be hoisted to a
tile" exists only because many operations share a decision; two arbitrary sets have no tile, and
probe-and-commit has nothing to amortise a probe over. §6's revised R4 already handles this as a
two-regime claim, but note what that costs: **the batch regime is the only one with evidence.** The
single-operation regime is asserted, not measured.

**3. The Roaring differentiator inverts.** `discussion.tex:25–27` claims the all-pairs batch setting
as novel against Roaring. Under the new frame *we* are the pairwise design, so the claim reverses.
What survives is the wider representation set, adaptive-versus-static selection, and finer
granularity — see the standing question in §10.

**4. Metadata amortisation — the one I had underweighted, and the one with a clean answer.**
`results_a.tex:27` says row metadata is "computed once at build time". Under the batch frame that is
amortised over `N` operations per row and is obviously free. **Under a one-shot operation on two
arbitrary sets, the build cost is charged to that single operation**, and the "selection is nearly
free" claim would collapse — you cannot spend `O(m)` building a zone map to save `O(m)` of work once.

> **Resolution, and it is better than what it replaces.** Metadata is part of the *representation*,
> built at ingest and amortised over the whole lifetime of the set — every operation that set ever
> participates in — not over a batch. **This is exactly the assumption Roaring already makes**: its
> container choice is made at ingest and amortised the same way. So the reframing does not weaken the
> argument, it makes it directly comparable to the baseline, and it removes the need to justify a
> batch at all.
>
> **What this obliges the paper to do:** state the ingest-time assumption explicitly and once, early;
> report build cost separately in the storage table (§24's metadata column already does this); and
> keep [GAP 4]'s end-to-end number honest by charging build cost there, where a single corpus is
> built once and operated on many times.

**Everything else in the inventory is wording** — `(i,j)` indexing, "ns per pair" axis labels,
"round-robin-interleaved repeats", and the operation-set commitments of §9c, which are all currently
*true* because only intersection has been measured ([GAP 11]).

### [GAP 12] Reconcile the analytic model with the measurements — OPEN, and load-bearing

**Status: open. Raised 2026-08-05 when the zone-map expectation curve was derived and immediately
disagreed with the project's own ablation by three orders of magnitude.**

| | |
|---|---|
| **What theory says** | Above its crossover a zone map costs at most $1 + 1/W_z$ relative to a plain bitmap pairing — a bounded overhead of about **0.2%** at $W_z = 512$. Derived, closed form, verified against simulation. |
| **What measurement says** | The ungated filter measured geomean **0.763×** with a worst case of **0.09×** — roughly **11× slower** — on `msprime_10k` (C29, `bench/filter_ablation.sh`). |
| **Gap** | Three orders of magnitude. |

**The model is not wrong; it is counting the wrong thing.** It bounds *work* — words touched — and
the measured penalty is memory-system behaviour that a work model cannot express: a filtered scan
surrenders sequential access and hardware prefetch, and pays indirection and a branch per bin. The
analytic curve and the measured curve are in different units, and the paper currently plans to plot
them on the same axes (Fig. 2a–b analytic, Fig. 2c–d measured).

**This must be resolved before Figure 2 can carry both, and it is not a presentation problem.** A
reviewer who reads the derivation and then the ablation will find the paper contradicting itself.

**Two ways to close it, and they are not exclusive:**

1. **Report measured *work*, not time, in the comparison panels.** Instrument the build path to emit
   words-touched per operation, which is what the model predicts, and plot that against the analytic
   curve. Clean, exact, and it makes the deviation-from-expectation argument airtight — but it does
   not by itself explain the time penalty.
2. **Extend the model with a measured per-bin constant.** Cost becomes
   $c_\text{seq}\,(\text{words scanned sequentially}) + c_\text{bin}\,(\text{bins visited})$ with
   $c_\text{bin}/c_\text{seq}$ measured per host. Fit it, report the constant, and state the density
   at which the *time* model — not the work model — crosses. This is the version the paper needs for
   its claims.

**What to measure.** Performance counters on the zone-mapped path against the plain scan, at
densities above the analytic crossover: cache misses, prefetch effectiveness, branch mispredictions,
and the realised cost per bin visited versus per word scanned. One targeted run, existing binaries.

**Why this is worth doing rather than eliding.** Reconciled, it becomes a third independent argument
for measured over modelled selection, and the strongest of the three: **even a correct analytic model
of the work underestimates the cost of choosing wrong by orders of magnitude, because the penalty
lives in the memory system rather than in the operation count.** That is a genuinely useful result
for anyone building an adaptive kernel, and it generalises well beyond this paper. Unreconciled, it
is the sharpest inconsistency in the manuscript.

**Scope note:** this applies to every analytic curve in Fig. 2, not only the zone map. Panels a and b
are work models; panels c and d are planned as measurements. Decide the units question once, for the
whole figure.

### Two structural decisions the gap register forced — settled here

**Gap 3 is resolved by reassignment, not by a seventh display item.** Fig. 3 panel (a) *is* the
strategy map — cell × corpus shape, coloured by which strategy class wins there — and the residency
curves move to panel (b). The count in prose was never the right carrier for the paper's most
reusable finding, and a map of which mechanism to reach for where is more useful to a reader than
the ratio it replaces. The display budget stays at six.

**Gap 4 is resolved by making the oracle a feature rather than a liability.** When the end-to-end
number lands, it does not sit alongside Table 1 — it *becomes* Table 1. The deployed selector's
ratio against tuned CRoaring is the headline column, with selection and build costs charged. The
best-cell oracle then returns as a second column, correctly labelled as an upper bound, and **the
gap between the two columns is the regret** — the same quantity Fig. 4 reports, now visible per
corpus in the paper's main table.

That reframing turns the paper's most serious weakness into one of its better ideas. Reporting
deployed performance beside the best achievable performance, on every corpus, is a stronger and more
honest presentation than either number alone, and it removes any temptation for a reader to compose
two numbers that were never measured together. It also gives R3 and R4 a shared spine: R3 establishes
what deciding costs, R4 shows what deciding is worth and how much is still on the table.

If the end-to-end harness does **not** land in time, Table 1 stays oracle-only, the oracle is
labelled as such in the column header rather than in a footnote, and the Discussion states plainly
that deployed end-to-end evaluation is the immediate next step. What is not acceptable is an
oracle-based Table 1 presented without that label.

---

### [SCOPE CHANGE, 2026-08-05] Size / cardinality pruning bounds are now in scope

**`PROBLEM_STATEMENT.md` §8 lists "Pruning bounds (Swamidass–Baldi)" as an explicit non-goal** —
"Real, and orthogonal: they change the complexity class by skipping pairs, whereas this work makes
each pair cheap. A hook exists in the thresholded API; in scope only after the pairing matrix
works." **The author has brought them in scope.** Recorded here so the change is not mistaken for
drift, and so `PROBLEM_STATEMENT.md` §8 can be reconciled deliberately rather than quietly.

**What was added, and where.** The size bound is rung 3 of §2's ladder, beside prefix filtering:
`J(A,B) ≤ min(a,b)/max(a,b)`, so `J ≥ t` requires `min(a,b) ≥ t·max(a,b)`, and `|A∩B| ≥ τ` requires
`min(a,b) ≥ τ`. Restricting the *inputs* to a size band is the same test applied earlier. Written at
three altitudes: Introduction (two sentences), Results (`results_b.tex`, thresholded-mode
subsection), Methods theory block (`theory_block.tex`, two new paragraphs with hypotheses,
Eq. `sizebound` / Eq. `survive`), Discussion (the ladder paragraph).

**The attribution is not ours and the paper says so in all four places.** The bound is
Swamidass & Baldi (`swamidass2007bounds`) and the size filter of Bayardo et al.
(`bayardo2007allpairs`, with `xiao2008ppjoin`). What the paper claims is its *placement* in the
ladder and its *composition* with the rest — not the bound.

**The `1/i` question, settled and better than expected.** `kernels/storm_gen.cpp` draws the 1/i
spectrum log-uniformly on `[1, C]`, i.e. `P(c) ∝ 1/c` exactly. So the log-uniform model is not a
stand-in for the mandated spectrum — **it is the mandated spectrum**, and
`surviving = 1 − (1 − ln(1/t)/ln K)²` applies directly. Verified against 10⁵-sample simulation to
three decimals at all five published points.

**Two caveats now in Methods, both load-bearing.** (i) Cardinalities are integers and `a = b` passes
at every `t`, so the continuous form is optimistic where sizes are small — 0.0185 rather than 0.0111
surviving at `t = 0.95, K = 10⁴` when drawn as the generator draws them. (ii) The favourable scaling
in `K` belongs to this spectrum, not to skew generally: under the *other* reading of "1/i" (ranked
sizes `a_i ∝ 1/i`, density `∝ 1/a²`) the surviving fraction is exactly `1 − t` independent of `K`.
The generalisation that covers both is `surviving ≈ 2 ln(1/t) ∫g²`, governed by the effective
log-spread of the size distribution.

**A composition claim was falsified and the granularity rule needed refining.** The expectation was
that a mechanism acting at the coarsest granularity multiplies cleanly with everything below it. It
does not. Size-bound survivors are the pairs with `a ≈ b`, and representation choice, zone maps and
prefix filtering are each cheapest when the sizes are *dissimilar* — so the composed cost exceeds
the product by `E[min(a,b) | survivor] / E[min(a,b)] → ln K / 2` (4.40 at `K = 10⁴, t = 0.9`).
Decorrelating the size test at the same survival rate restores the product to within 0.6%, which
identifies the shared *statistic* rather than the shared granularity. **Revised rule now in the
paper: mechanisms multiply when they act on different waste AND read different operand statistics;
different granularity is necessary, not sufficient.** The composition is still worth taking — 10.0%
of the better single mechanism.

**Not written, because it could not be verified.** The brief proposed "every other mechanism in the
paper degrades as data becomes more heterogeneous; this one improves". That contradicts the
Discussion's own heterogeneity paragraph ("the property doing the work throughout this paper is
heterogeneity, not sparsity") and did not survive checking — under Jensen, prefix filtering also
improves with size spread. What is written instead is the defensible form: **the size bound is the
only mechanism whose value is a function of the dispersion of cardinalities across the corpus rather
than of within-row arrangement at a fixed density, and it strengthens monotonically as that
dispersion widens.** Different axis, not a competing one.
