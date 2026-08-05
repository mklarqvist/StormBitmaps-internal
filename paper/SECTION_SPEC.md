# StormBitmaps Paper — Section Content Specification

Level-2 document (`nature-methods-manuscript-writer`). Specifies what each section must **contain**,
not how any sentence is phrased. Works inside the skeleton and claim ledger already fixed by
`NARRATIVE.md`; does not move any boundary NARRATIVE.md set. Where this document gives a number, it
is a **typed slot** — `[NUM: description]` — because `results/` has been deleted and the campaign is
being re-run. No numeric value below is to be typeset as-is.

Authority order unchanged: `PROBLEM_STATEMENT.md` > `RESEARCH_PLAN.md` (latest-numbered section
wins) > `NARRATIVE.md` > this document. This document adds no new claims and settles no new
ordering; it only unpacks NARRATIVE.md's skeleton into paragraph-level content instructions.

---

## 0. Word and item budget carried in from NARRATIVE.md §1, §5, §6

| Section | Budget |
|---|---|
| Introduction (unheaded) | ~500–650 words, 5–6 paragraphs |
| Results (6 subsections + lead-in) | ~2,900–3,200 words |
| Discussion (unheaded) | ~700–900 words, 4–5 paragraphs |
| Methods | ~2,500–3,000 words, excluded from the main-text cap |
| Main text total (excl. Methods, refs, legends) | ~4,000–5,000 words |
| Main display items | 6 (Figs. 1–4, Tables 1–2) — no more, no fewer |

---

## 1. Introduction — paragraph inventory

Unheaded, opens directly after the abstract. The skill's default methods-paper funnel is **broad
need → existing capability → concrete technical limitation → unmet methodological requirement →
present contribution → validation scope**. NARRATIVE.md's skeleton is a five-move version of the
same funnel with the Roaring framing-device (§3) grafted onto the limitation step. Reconciled below
into 6 paragraphs; a 5-paragraph cut is acceptable by merging P2 and P3.

### P1 — Broad need (2–3 sentences)

**Job:** state the all-pairs set-intersection cardinality problem and why it is computed at scale.
**Must establish:** `|Xᵢ ∩ Xⱼ|` over many rows is a primitive underlying similarity search, record
linkage, graph analytics and population-genetic pairwise statistics (cite Bayardo/Ma/Srikant, Xiao
et al., Swamidass–Baldi as the domains that need it — see NARRATIVE.md §7). Do **not** name genomics
as the motivating case (NARRATIVE.md §9.4) — name it later, in Results, as one corpus family among
several.
**Reader must believe by the end:** this is a real, general, recurring computational primitive, not
a niche one.
**What it must NOT do:** open with a "rapidly growing field" claim (skill DON'T list) or a
literature catalogue.

### P2 — Existing capability: fixed-cost bitmap AND+popcount (2–3 sentences)

**Job:** state what the standard approach does and why it is attractive.
**Must establish:** `popcount(A AND B)` over dense bitmaps is Θ(m) regardless of cardinality; cite
Muła/Kurz/Lemire 2018 explicitly and **credit it as solved** — the paper must not appear to
re-litigate popcount speed. This paragraph is where the reader learns the cost model vocabulary
(`m`, density) that Results reuses without redefinition.
**Reader must believe:** fixed-cost bitmap AND is fast, well-understood, and the default for good
reason.

### P3 — The concrete limitation: fixed cost ignores skew (2–4 sentences)

**Job:** turn P2's strength into the exposed weakness.
**Must establish:** real degree/frequency distributions are skewed (cite Fu 1995 for the population-
genetic instance, generalize to any 1/i-shaped or power-law spectrum); under skew, most rows are
sparse, most pairs have a near-empty side, and Θ(m) work is spent ANDing zeros against zeros. State
the magnitude qualitatively ("orders of magnitude," not a specific ratio — no headline number
belongs in the Introduction per NARRATIVE.md §9.1).
**Reader must believe:** the fixed-cost default is *wasteful*, specifically and mechanistically, not
just "could be faster."

### P4 — Existing adaptivity and its own fixed commitment: Roaring (3–4 sentences)

**Job:** introduce the state of the art fairly, then expose its own fixed granularity as the
residual gap.
**Must establish:** Roaring (cite Chambi et al. 2016, Lemire et al. 2018) already dispatches
per-chunk on container type — so naive "just pick a representation" is *not* new. Then state the
foil precisely, as NARRATIVE.md §3 insists: Roaring commits to 2¹⁶-bit chunks and a **fixed**
cardinality threshold, chosen once at ingest, for a workload built around one pairwise operation. At
N² all-pairs scale with a large universe, that granularity can misprice global density — flag this
as the mechanism Results §5 (R5) will test, without giving the census1881 number here.
**Explicit instruction:** state "Roaring is excellent" or equivalent — the foil must read as
generous, per NARRATIVE.md §3's explicit framing rule. Do not use language that reads as "Roaring is
slow."
**Reader must believe:** the state of the art already solved per-chunk representation choice; what
it has not solved is whether that choice is fine-grained enough and cheap enough at N² scale.

### P5 — The two framing questions (2 sentences, verbatim device from NARRATIVE.md §3)

**Job:** pose the two objections in the paper's own voice, exactly as Roaring's introduction does.
**Content, adapted from NARRATIVE.md §3:**
1. Given that popcount is solved and Roaring already dispatches per chunk, is there anything left
   for representation choice to buy?
2. If a wider representation matrix does buy something, can the choice itself be made cheaply enough
   at N² scale that the saving survives the deciding?

**Structural requirement:** these two sentences are not rhetorical — Results R1–R2 answer question 1
and R3 answers question 2 (NARRATIVE.md §3). The Introduction must not answer them; it only poses
them.

### P6 — Contribution and roadmap (3–4 sentences, "Here, we…" register)

**Job:** state what was built, what was measured, and preview the finding order once.
**Must establish, in this order:**
1. what was measured — ten representation pairings (define the count here; see Methods §2 for the
   eleven-vs-ten reconciliation) across the full density range, four microarchitectures, seventeen
   real corpora;
2. the headline finding, stated as a comparison not a ratio: a SIMD-throughput kernel wins none of
   the asymmetric measurement points, while representation selection wins on every real corpus
   tested;
3. one sentence naming that the paper also reports where the approach does not pay (sets up R6 /
   Discussion limitations without detail).
**Explicit exclusions:** no specific speedup number, no genomics framing, no claim of a shipped
library or "we release" language (NARRATIVE.md §8's recommended-option (a) constraint — see §6 of
this document, claim-calibration table, row on "artifact").
**Reader must believe, closing the Introduction:** exactly what will be tested, in what order, and
what kind of finding (a systematic map of which mechanism wins where) to expect — nothing about
magnitude yet.

**Paragraph-count fallback:** if word budget forces a 5-paragraph cut, merge P2 and P3 into one
paragraph (existing approach stated, then its failure mode in the same paragraph) — do not merge P4
with anything; the foil paragraph is load-bearing for the whole paper's positioning and must stand
alone.

---

## 2. Results — six subsections, R-A-F-I

### Results lead-in (before R1, unheaded, 2–4 sentences — required by the skill, not explicitly
reserved by NARRATIVE.md's skeleton; budget it out of R1's allowance)

Per the skill's "Results opening" requirement: state the evaluation sequence and define any term
used without redefinition thereafter. Content:
- state that the evaluation proceeds synthetic → real, controlled density → controlled strategy →
  charged selection cost → real corpora → competitor mechanism → adversarial cases (this is
  NARRATIVE.md §5's "escalation ladder," compressed to one sentence);
- define "cell" (a representation pairing, e.g. B×S) and "pairing matrix" here, once, so R1–R6 never
  re-define them;
- state once that all ratios reported are against a `run_optimize`-tuned CRoaring baseline unless
  stated otherwise (pre-empts needing to repeat "tuned" in every subsection).

Do not preview numeric findings here — that violates the skill's "do not preview every numeric
result" rule.

---

### R1 — The cost of intersection is U-shaped in density

**Carries:** A2. **Display item:** Fig. 2 (density sweep). **Separating axis:** none — this is the
opening subsection; it establishes the axis (distance from density ½) that every later subsection is
implicitly compared against.

**Reason (opening sentence's job):** pose the local question directly — does bitmap×bitmap's Θ(m)
cost actually hold across the density range it is assumed to hold across, and where do the other
nine cells beat it? Pattern: "To characterize when a fixed-cost kernel is the wrong choice, we swept
density from one bit set to all bits set and measured every cell's cost at each point."

**Approach (Action):** name the sweep — both sides at matched density, universe held fixed so cache
residency does not confound the density axis (state that constancy explicitly; it is what makes the
sweep clean), all ten non-Roaring cells raced at each point. State that both uniform and 1/i spectra
are swept per AGENTS.md rule 4 — if only one is shown in Fig. 2, say which and point to
Supplementary for the other.

**Findings, in order:**
1. Bitmap×bitmap cost is flat across the density sweep — report as a range in ns/pair, not a point
   estimate: `[NUM: B×B ns/pair range across five orders of magnitude of density]`. This is the
   fixed-cost premise **as a measurement**, language echoing PROBLEM_STATEMENT.md §2.5's framing
   ("no longer an argument").
2. At the sparse extreme, the best cell beats B×B by `[NUM: best-cell-vs-B×B ratio at sparsest
   density point]`, and state that the ratio grows with universe size (qualitative trend, not a
   second number unless independently measured).
3. The crossover density is stated as an approximate value with the word "approximately" —
   `[NUM: crossover density, ~0.4% order of magnitude]` — per the claim-stability table
   (NARRATIVE.md §4): the exact crossover is fragile under re-measurement, only its existence and
   rough location are stable.
4. The dense tail: state that near density 1 the *complement* is sparse and R×R (or the complement
   cell, if C4/complement representation is in scope for this draft — confirm against Methods §2
   before finalizing) wins by `[NUM: dense-tail win ratio]`. This is the sentence that makes the
   curve U-shaped rather than monotonic — flag it as the paragraph's central move.

**Interpretation/limit sentence (Inference, closing):** state the reframe explicitly, because it
corrects PROBLEM_STATEMENT.md §2's own earlier framing: the operative variable is distance from
density ½, not sparsity per se — a row that is 99.99% ones is exactly as compressible as one that is
0.01% ones. Do not yet say *why* (mechanism) — that is Methods/Discussion territory.

**Target:** 3 paragraphs, ~430–470 words. Paragraph split: (i) Reason + Approach, (ii) Findings 1–2,
(iii) Findings 3–4 + Inference.

---

### R2 — Avoiding work beats accelerating it, and the gap widens with working-set size

**Carries:** A1, A8. **Display item:** Fig. 3 (two panels: SIMD advantage decay; work-avoidance
advantage over the same axis). **Separating axis:** strategy compared, not data (NARRATIVE.md §5) —
R1 varied density on one strategy set; R2 fixes density regimes and instead compares *which kind* of
speedup (throughput vs. avoidance) wins, across working-set size and across three ISAs.

**Framing instruction, non-negotiable:** this subsection is a **positive claim about mechanism**, not
a report of a failed hypothesis. Every sentence should read as "the kernels that win are the ones that
skip work" — never as "SIMD loses" or "vectorization fails." The finding is *why the asymmetric cells
are built around rank indexes and zone maps rather than vector width* (NARRATIVE.md §2's first
surviving justification-tested claim). The word "negative" must not appear in this subsection, and
none of the individually refuted micro-optimizations (Harley-Seal accumulation, naive run-collapsing,
register blocking, prefetch behavior) belong here — none of them justifies a design decision that
survives into the final system, which is the only test NARRATIVE.md §2 sets for inclusion. They are
cut to Supplementary at most, not summarized in Results.

**Reason:** "Having established that most cells beat bitmap×bitmap (R1), we asked what mechanism is
responsible for the asymmetric cells' advantage, since that answer determines what those kernels
should be built around." This is the subsection that answers framing question 1 fully (together with
R1).

**Approach:** state the asymmetric-cell race — all five asymmetric cells (B×S, B×R, B×W, S×R, S×W —
confirm exact list against Methods §2) at five corpus shapes, 25 measurement points, work-avoidance
strategies (rank lookup, zone-map skip, galloping search) raced against SIMD-throughput variants of
the same cells. Separately, state the working-set sweep: the same comparison repeated as the data
footprint crosses L1→L2→DRAM, on three microarchitectures (name them once here; Methods carries the
exact core/cluster identifiers).

**Findings, in order:**
1. The mechanism that wins, stated as the positive result it is: across the 25 asymmetric measurement
   points, the winning kernel is a rank lookup, a zone-map skip or a search-strategy choice —
   `[NUM: count of points won by a skip-based strategy, expected shape "25 of 25" or "nearly all of
   25" per the claim-stability table]`. State this as *what the asymmetric cells are for*: they are
   proportional to the small side already, so the available saving is in not touching the large side
   at all, and the measurement confirms that is where the winning kernels put their effort.
2. Working-set widening: the gap between the two strategies widens as working set grows, on **every**
   microarchitecture tested. Report both halves of the same trend as one comparison, not two separate
   findings: SIMD-throughput advantage on the one cell where work is irreducible (B×B) shrinks as
   working set grows — `[NUM: SIMD advantage, small→large working set, per ISA, 3 hosts]` — while the
   work-avoidance advantage on the asymmetric cells holds or grows over the same axis —
   `[NUM: work-avoidance advantage, small→large working set, per ISA, 3 hosts]`.
2a. **Calibration note for the drafter, not for the manuscript:** the "grows" half of this trend was
    ARM-only in the prior campaign (flat on the x86 host). Report per-host, and if it reproduces only
    on some hosts, state which ones rather than generalizing from the ARM result — this is the pattern
    RESEARCH_PLAN.md §14.2 (C2) already flagged as an earlier overclaim on this exact axis. The
    subsection heading's claim ("the gap widens") must hold in whatever weaker form the data actually
    supports — "holds or grows," not "grows," if that is what reproduces.

**Interpretation/limit sentence:** the two mechanisms scale in *opposite* directions with the memory
hierarchy — the cell where the work is irreducible gets relatively less benefit from vector width as
the hierarchy gets less friendly to it, while the cells built around skipping keep or increase their
edge. State this as the sentence Discussion will later use to predict forward to future cores (do not
make that prediction here — Results states what was measured, not what it implies for hardware
trends).

**Target:** 3 paragraphs, ~430–480 words. Split: (i) Reason+Approach, (ii) Finding 1, (iii) Finding 2
+ Inference.

---

### R3 — Selection must be hoisted, and measured rather than modelled

**Carries:** A5, A6, A9. **Display item:** Fig. 4 (selection cost + regret, three panels/hosts) covers
A5 and A6. A9 (the gating finding) has **no dedicated main figure** per NARRATIVE.md §6 — its
evidence lives in Supplementary ("zone-map gating ablation") and is cited by name; state the finding
in prose with the headline geomean/worst-case/harmed-count numbers, not as an implied figure
citation. **Separating axis:** cost of *deciding*, not of computing (NARRATIVE.md §5) — R1–R2 charged
only kernel cost; R3 is the subsection that answers framing question 2 by charging the decision
itself.

**Reason:** "The cells above are only a saving if choosing among them is nearly free. We measured
the cost of the choice itself, at three granularities." Frame explicitly against a numeric budget
(2% of runtime — cite this as the paper's own stated budget, from PROBLEM_STATEMENT.md P2/§6, not as
an externally sourced figure).

**Approach:** name the three selection granularities under test — per-pair (a decision every (i,j)),
per-tile (hoisted out of the inner loop across a pre-partitioned block), probe-and-commit (the
Micro-Adaptivity-style ε→0 policy — cite Răducanu/Boncz/Żukowski here by name per NARRATIVE.md §7).
State the regret metric: cost relative to a bucket oracle that already knows the winning cell, on the
same axis as the selection-cost measurement, on three hosts.

**Findings, in order:**
1. Per-pair selection fails the budget: `[NUM: per-pair selection cost, % of runtime, range across
   hosts/corpora]` — state plainly that this exceeds 2% and is therefore a failed policy, not a
   qualified one.
2. Hoisted policies pass, by roughly an order of magnitude of headroom: per-tile
   `[NUM: per-tile selection cost %]`, probe-and-commit `[NUM: probe-and-commit selection cost %]`,
   both against the 2% line — this is the finding Fig. 4 must make visually obvious (all bars under
   the line except per-pair).
3. Measured selection beats modelled selection: report regret against the bucket oracle for
   probe-and-commit, per-tile, and (if in scope) the all-bitmap-always baseline and a closed-form
   per-pair cost model — `[NUM: regret %, probe / per-tile / all-bitmap / analytic model]`. If the
   model-worse-than-no-selection finding reproduces, state it plainly — it is the direct justification
   for probe-and-commit over a cost model, which is the second of NARRATIVE.md §2's three
   justification-tested claims, and it earns its place because it justifies that design choice, not
   because it is a striking number on its own.
4. The filter must be gated — the third of NARRATIVE.md §2's three justification-tested claims, and
   stated as a positive design conclusion, not a caveat: applying the zone-map filter unconditionally
   is a net loss across the corpus set, while gating it on a cheap online test never loses and often
   wins large. Report the unconditional-vs-gated contrast — `[NUM: geomean, filter always-on vs
   gated]`, `[NUM: worst-case ratio, always-on]`, `[NUM: count of corpora harmed, always-on vs
   gated]`. This is the finding that justifies why the filter is applied conditionally rather than as
   a fixed component, and it must be given equal prominence to finding 3, not folded into a footnote.
   Detail beyond this headline contrast (the per-corpus breakdown of where gating helps vs. bypasses)
   belongs in Supplementary, cited by name, not reproduced in Results.

**Interpretation/limit sentence:** selection is not solved, only no longer harmful — state this using
language calibrated to L1 (nothing in the campaign lands within roughly 1.5× of the oracle). This is
a deliberately modest closing sentence; do not let it read as "selection is solved."

**Target:** 4 paragraphs, ~520–580 words. Split: (i) Reason+Approach, (ii) Finding 1–2, (iii) Finding
3, (iv) Finding 4 + Inference.

---

### R4 — The winning representation pairing migrates across real corpora

**Carries:** A3, A4. **Display item:** Table 1 (17 corpora × universe/density/winning cell/vs
CRoaring/vs all-bitmap). **Separating axis:** real data, not synthetic sweeps (NARRATIVE.md §5) —
R1–R3 are entirely synthetic; R4 is the pivot to evidence a reviewer cannot dismiss as generator
artifact.

**Reason:** "The sweeps above use generated data, which can encode the structure a method is built
to exploit. We therefore repeated the comparison on corpora with independent provenance" — name the
provenance move explicitly (CRoaring's own default benchmark corpora, `real-roaring-datasets`, plus
larger modern graphs), because it is the paper's answer to the generator-artifact objection.

**Approach:** state corpus count (17), density/universe span (report as a range, e.g.
`[NUM: density span, min–max across 17 corpora]`, `[NUM: universe span]`), and that each corpus is
run against the same tuned-CRoaring baseline as R1–R3's synthetic comparison (consistency of
baseline across subsections is itself a methodological point worth one clause).

**Findings, in order:**
1. Headline win count, stated as a fraction, not a single ratio: adaptive pairing beats tuned
   CRoaring on `[NUM: count]` of 17 corpora, range `[NUM: ratio range]×`, median `[NUM: median
   ratio]×` — cite Table 1 immediately after this sentence, per the skill's rule that the citation
   follows the supported statement.
2. **Precision discipline (L6), stated in-line, not deferred:** state that per-corpus ratios are
   reported as medians over repeats, never to two significant figures, and name which corpora (if
   any, per the re-measurement) are excluded from the headline range as unquotable pending a harness
   fix — do not silently drop corpora from Table 1 without a footnote explaining why.
3. Winning-cell distribution — this is the sentence NARRATIVE.md §6 calls "the paper," more load-
   bearing than any ratio: report the count of corpora won by each cell,
   `[NUM: winning-cell histogram, e.g. B×B ×n, B×S ×n, B×R ×n, S×S ×n, R×R ×n]`, and state plainly
   that no single cell dominates — if one did, restate that this would refute the pairing-matrix
   thesis (this is the falsifiability sentence and must appear, per NARRATIVE.md §6's explicit
   instruction: "had one column dominated, the paper would refute itself").
4. The winner migrates with density: state the direction (sparse end favors B×R/S×S/B×S-type cells;
   dense end favors B×B) as a qualitative trend backed by Table 1, and name the worst-losing column
   (e.g. W×W) with its loss rate, `[NUM: fraction of corpora where the worst cell underperforms 1.0×]`
   — this is the "cost of choosing wrong" sub-claim and must be stated, not omitted.

**Interpretation/limit sentence:** state that the map — which cell wins where — is itself the
result, more informative than any single headline ratio; this sentence should echo, not repeat
verbatim, the Table 1 callout already given in Finding 3.

**Target:** 4 paragraphs, ~600–650 words (largest budget of the six — Table 1 is the paper's central
display item and needs room to be discussed, not just cited). Split: (i) Reason+Approach, (ii)
Finding 1–2, (iii) Finding 3, (iv) Finding 4 + Inference.

---

### R5 — Fixed chunk granularity misprices dense corpora

**Carries:** C11 (flagged in NARRATIVE.md §3 for independent re-verification before this subsection
is finalized — **do not draft final prose here until that verification is repeated against current
CRoaring `master`**). **Display item:** none new — this subsection interprets Table 1's dense-corpus
rows plus a container census (a Supplementary or in-text small table, TBD by paper-figures skill;
default to in-text if it is under ~5 rows, per NARRATIVE.md's 6-item cap). **Separating axis:**
competitor's mechanism, not ours (NARRATIVE.md §5) — the only subsection whose subject is *why*
CRoaring loses on specific corpora, mechanistically.

**Reason:** pose the puzzle directly: some dense corpora (6–17% global density) show CRoaring losing
by more than a same-density popcount workload should explain — why? This is the one Results
subsection allowed to open on an anomaly rather than a stated hypothesis, because the anomaly *is*
the finding.

**Approach:** name the diagnostic — a per-container census of CRoaring's own internal representation
choice after `run_optimize()`, via its own statistics API, on the corpora where the gap is largest.
State this is inspecting the competitor's own instrumentation, not ours (methodological credibility
point).

**Findings, in order:**
1. State CRoaring's container-selection rule precisely: per-2¹⁶-bit-chunk cardinality tested against
   a fixed threshold (name the threshold value, sourced from CRoaring's own code — this is a tier-2
   citation of vendored source, not a measurement, and must be labelled as such per §5 of this
   document).
2. Report the census for the corpora in question: array/bitset/run container counts at stated global
   density, e.g. `[NUM: container census table, corpus × {array, bitset, run} counts]`. Name the
   sharpest case explicitly if it reproduces: a corpus with **zero** bitset containers despite
   percent-level global density.
3. State the mechanism in one sentence: with a large universe, mass spreads thinly enough that
   individual chunks stay under the fixed threshold even when global density is high — so CRoaring
   runs array merges where a bitmap popcount would be correct.

**Interpretation/limit sentence:** state this is a concrete, mechanistic, *observed* (not inferred)
property of a fixed-granularity, fixed-threshold design — and, per NARRATIVE.md §3's explicit
instruction, immediately qualify that this is not evidence Roaring is poorly engineered; it is
evidence that a per-chunk fixed threshold chosen once at ingest cannot be correct for every global
density simultaneously. **Do not let this subsection close without that qualifying sentence** — it
is the difference between a fair foil and an unfair one.

**Target:** 3 paragraphs, ~380–430 words (shortest of the six — it is a sharp, single-mechanism
subsection, not a multi-finding one). Split: (i) Reason+Approach, (ii) Findings 1–2, (iii) Finding 3
+ Inference.

---

### R6 — Where the method does not pay

**Carries:** L1, L2, L3. **Display item:** none new (references Table 1's outlier rows and Table 2
if the storage limitation is pulled forward here rather than left to Discussion — default: keep L3
here since it is a measured limitation, leave the *implication* for artifact status to Discussion).
**Separating axis:** scope (NARRATIVE.md §5). **Structural role:** the concessive bridge into
Discussion — NARRATIVE.md §5 specifies it opens with "Although…", which is unusual for a Results
subsection opener and should be treated as a deliberate exception to the skill's usual Reason
patterns.

**Framing instruction, non-negotiable, per the author's explicit correction:** this subsection
**states scope, not experiments that failed.** It is a limitations subsection in the ordinary sense —
short, and built from three bounded scope statements, not a recap of the project's optimization
history. Nothing cut from the paper under NARRATIVE.md §2 (Harley-Seal, register blocking, prefetch,
the losing per-cell variants, the port-pressure investigation) belongs here or anywhere else in
Results — R6 draws only on findings already established in R1–R4 and on the two remaining measured
limitations (L1, L3), not on discarded experiments.

**Reason:** open with the concessive construction directly, e.g. "Although representation selection
won on every real corpus tested (R4), it does not win everywhere, and its domain is worth stating
precisely." This is the one subsection whose Reason is explicitly framed as a scope-narrowing move
rather than a new question.

**Approach:** no new experiment — this subsection restates, at Results-closing register, three scope
boundaries already established or implied earlier in the section.

**Findings, in order (three scope statements, each one to two sentences — keep this subsection
short):**
1. **The mid-density band belongs to bitmap×bitmap.** This is R1's U-shaped finding restated as a
   scope boundary rather than a fresh claim: between the two crossovers, nothing beats B×B, and the
   method's job there is to identify that band cheaply and get out of the way, not to beat it.
2. **Layout determines regime (L2).** State plainly, with the concrete real-corpus contrast: the same
   underlying data can sit deep in the sparse regime or in the B×B-correct mid-band depending on row
   orientation — name the haplotype-major real-corpus point where all-bitmap wins outright,
   `[NUM: haplotype-major ratio, expected shape "below 1.0×"]`, against the variant-major real-corpus
   points at `[NUM: variant-major ratio range]`. This paper does not get to choose the layout a
   downstream dataset arrives in.
3. **The selector is cheap but not optimal (L1).** State this once, briefly: nothing measured lands
   within roughly 1.5× of the bucket oracle (R3). This is a scope statement about the selector's
   maturity, not a restatement of R3's evidence — do not re-derive the regret numbers here, one clause
   pointing back to R3 suffices.

**Optional fourth statement (L3, storage/metadata) — include only if word budget allows without
crowding the three above:** selector metadata is universe-proportional and currently unguarded,
sharpest measured case `[NUM: metadata-to-data ratio, largest-universe sparse corpus]`, citing Table
2. If cut for length, this limitation still must appear in Discussion's limitations paragraph (P5) —
it cannot be dropped from the paper entirely, only relocated.

**Interpretation/limit sentence:** close by stating that these boundaries define the claim's domain
rather than undermine it — selection wins broadly, on a defined domain, with a known and stated edge
— and hand off to Discussion for what the domain and the artifact gap mean going forward (do not
discuss the artifact gap here; that is Discussion's job per NARRATIVE.md §5).

**Target:** 2–3 short paragraphs, ~300–360 words — shorter than the original 430–480 estimate,
because this subsection is explicitly scope, not a synthesis of experiments. Split: (i)
Reason+Approach+Findings 1–2, (ii) Finding 3 (+ optional 4) + Inference.

---

### Results section total

R1 (~450) + R2 (~455) + R3 (~550) + R4 (~625) + R5 (~405) + R6 (~330) + lead-in (~80) ≈ 2,895 words
against the ~2,900–3,200 budget in §0 — comfortably inside it, with headroom to expand R4 (Table 1's
subsection) if the final campaign's winning-cell histogram needs more room to discuss. Cutting the
negative-result roundup from R2 and shortening R6 to pure scope freed the budget that the six-item
cap needs for Table 1 and Fig. 2.

---

## 3. Discussion — paragraph inventory

No subheadings. NARRATIVE.md §2b closes with an explicit, specific Discussion content plan —
newer and more concrete than §5's older "material absent from Results" summary — and this section is
built directly from it, mapped onto the skill's six functional slots (central contribution;
performance and comparison; generality and use; limitations; future work; closing implication). Two
slots are merged to fit the 4–5 paragraph budget; none are dropped.

| Skill slot | NARRATIVE.md §2b content item | Paragraph |
|---|---|---|
| 1. Central contribution | "Compress to two sentences" | P1, opening two sentences only — not a full paragraph |
| 2. Performance and comparison | "the same shape as Roaring's own contribution" | P1 (remainder) / P2 |
| — (new, not a default skill slot) | "the boundary between work avoidance and work acceleration is the transferable result... the memory-hierarchy trend moves it further toward avoidance every generation" | P3 |
| 3. Generality and use | "heterogeneity, not sparsity, is the property that makes representation choice pay, which predicts where else this applies" | P4 |
| 4. Limitations + 5. Future work + 6. Closing implication | "a short, unapologetic statement of what remains: the selector is cheap but not optimal, and the machinery is not yet reachable behind the C ABI" | P5 |

**Note on the skill's "Discussion does not introduce new results" rule.** P3 (the work-avoidance/
work-acceleration boundary and its memory-hierarchy trend) must not present *new measurements* — the
data it draws on (SIMD decay vs. work-avoidance hold/growth, A8) was already reported in R2. What is
new in P3 is the *forward-looking interpretation* — reasoning from a measured trend to what it
predicts about cores with even larger cache-to-core ratios. Keep the sentence boundary between "as
measured" (already stated, present-tense reference back to R2) and "which suggests" / "predicts"
(interpretation, hedged per §5 below). This is the one place in the paper where a predictive claim
about untested hardware is allowed, and it must be clearly flagged as prediction, not measurement.

### P1 — Central contribution, compressed to two sentences, then the Roaring-shape point

**Job:** state, in exactly two sentences, what the paper established — representation selection
dominates kernel acceleration in this regime, evidenced by the work-avoidance-wins /
selection-wins-on-every-corpus contrast (R2, R4). A third sentence opens the paragraph's second half:
**this is the same shape as Roaring's own contribution** — array containers, bitset containers, run
containers and SIMD intersection were each individually known, and what made Roaring land was the
composition, evaluated as one system, not any one part. State that this paper's claim is structured
the same way and should be read the same way.
**Must NOT:** re-derive the argument or re-cite figures — this is compression, not summary.

### P2 — Relationship to Roaring, stated generously and precisely (if not fully absorbed into P1)

**Job:** complete the positioning move P1 opened, if it needs more than the one sentence P1 affords.
**Must establish, in order:**
1. Concede plainly that Roaring already does adaptive per-chunk representation — the contribution is
   not "adaptive representation" as a category.
2. State precisely what is different: a wider representation set (adding W and the complement
   strategy to Roaring's three), cardinality-only specialization enabling the rank-index shortcut (§4
   of PROBLEM_STATEMENT.md), the all-pairs batch setting that Roaring's pairwise design does not
   address, and R5's C11 finding as the sharpest mechanistic instance of the granularity gap.
3. One sentence crediting Roaring's engineering and community success explicitly (NARRATIVE.md §3's
   "generous foil" instruction applies here even more than in Introduction P4, because Discussion is
   where a paper is most likely to read as dismissive if it is not careful).
**Calibration:** "demonstrated" language only for what R4/R5 actually measured; do not extend the
claim to "Roaring should adopt this" or similar prescriptive language.
**If P1's Roaring-shape sentence already carries this paragraph's full weight, merge P1 and P2 into
one paragraph rather than force two — the 4-paragraph floor of the budget permits this.**

### P3 — The work-avoidance/work-acceleration boundary, and what it predicts for future cores

**Job:** state the paper's most transferable finding as a design principle, then extend it forward.
**Must establish, in order:**
1. Restate the boundary, at Discussion register, as the transferable result: across the asymmetric
   cells, the win comes from deciding not to do work, not from doing the same work faster (this is
   NARRATIVE.md §2b's "second answer," and it should read as the paper's central mechanistic claim,
   not as a residual point).
2. State the memory-hierarchy mechanism in one sentence: vector throughput is a per-cycle constant
   that a widening core keeps buying more of; work avoidance is a *count* of memory-hierarchy
   crossings that widening a core does not reduce.
3. State the forward-looking prediction: as core width and cache-to-DRAM latency ratios continue to
   diverge, the measured gap should widen further, not narrow.
**Calibration:** step 3 is a "supported interpretation" per the three-level split in §5 below — use
"suggests" / "is consistent with," never "will" or "proves."

### P4 — Heterogeneity, not sparsity, is what makes representation choice pay

**Job:** generalize the paper's central finding beyond set-intersection cardinality, using the
correct organizing variable.
**Must establish:** state that the property doing the work throughout this paper is *heterogeneity* —
that real corpora mix rows of very different densities under one universe — not sparsity per se
(sparsity is what heterogeneity produces at one end; R1's dense tail is what it produces at the
other). This is a precise, falsifiable generalization: predict that representation choice should pay
wherever a workload mixes elements of very different densities under a shared cost model, and name
one or two adjacent domains where that condition plausibly holds (e.g. sparse linear algebra format
selection, columnar query pruning) **only in "anticipated use" register** — do not claim these were
tested.
**Calibration:** this paragraph is the one most at risk of overclaiming; every sentence past the
restated finding must use "anticipated use" verbs ("may extend to," "is consistent with"), never
"demonstrates" or "shows."

### P5 — What remains: the selector, and the artifact gap, stated once, briefly

**Job:** close the paper with a short, unapologetic statement of scope and status — NARRATIVE.md §2b
is explicit that this is brief, not a second limitations section restating R6.
**Must establish, in order, each as one declarative sentence, not elaborated:**
1. The selector is cheap but not optimal (L1) — one clause, pointing back to R3/R6 rather than
   re-deriving the regret numbers.
2. The scope boundaries not already fully spent in R6 — single-threaded throughout (L4); the
   real-corpus run is one microarchitecture while the synthetic comparison spans four (L5); the tile
   pipeline measures within-tile pairs only (L8). State each as a boundary, not an apology.
3. **The artifact gap, in the register NARRATIVE.md §8 recommends:** the pairing matrix, the gate and
   the tile pipeline are not currently reachable through the library's public C ABI; what this paper
   reports is a systematic measurement of the mechanism, not a released tool. Use neither "library"
   nor "we release" in connection with the pairing-matrix work (NARRATIVE.md §8's explicit
   word-choice instruction) — describe the system as a system under active engineering, with the
   artifact work sequenced immediately after.
4. Name the ABI/bindings work (RESEARCH_PLAN.md §16.4–16.5) as the concrete next step, and the
   Tomahawk follow-on as the eventual application layer, explicitly **not** motivating this paper
   (PROBLEM_STATEMENT.md §8's non-goals — do not let genomics re-enter as a motivating frame here
   after Results correctly scoped it as one corpus family among several).
**Closing sentence:** the map (which pairing wins where) is transferable independent of any single
number in it, and that is the paper's most durable contribution.
**Calibration:** candor, not apology — one declarative sentence per point, no hedging stacked on
hedging.

### Discussion DOs and DON'Ts specific to this paper

- **Do not include any methodological remark about the project's own history of corrected or
  retracted claims** (the four genomic-claim corrections, the port-pressure inference). NARRATIVE.md
  §9's reconciliation list states this explicitly: "the correction record stays out of the
  manuscript." A version of this document written before the author's correction included such a
  paragraph; it is cut. Reporting rules derived from that history (median-not-mean, ranges-not-point-
  ratios) still apply throughout the paper — only the narrative-of-the-history is excluded.
- **Do not let P4's generalization reintroduce genomics as a motivating example.** Genomics already
  appears correctly scoped in R4/R6 as one corpus family among several; Discussion's job is to
  generalize the *mechanism* (heterogeneity), not to revisit any one corpus.

### Discussion total

Target 4–5 paragraphs (P1–P2 may merge to 4; P1–P5 in full is 5), ~700–800 words — trimmed slightly
from the original 750–850 estimate now that the genomic-correction paragraph is cut and P5 is
explicitly brief rather than a second limitations section.

---

## 4. Methods — subheadings and O-P-Q content

Order fixed below; do not reorder without flagging to level-1 (`manuscript-architecture`) first,
since Methods order is itself part of the settled skeleton per NARRATIVE.md §5. ~2,500–3,000 words
total; the skill's O-P-Q model (Orientation, Procedure, Quality/analysis) applies to each.

### 4.1 Representation definitions and cost model

**Orientation:** one to two sentences only if a reader outside bitmap-index literature would not
otherwise know what "representation" means here — likely needed, since the paper's whole apparatus
hinges on the term. Template: "Each row is stored in one of several representations, each with a
distinct asymptotic cost for intersection cardinality; we define each below before describing how
cells are paired."
**Procedure:** formal definition of each representation in the matrix — B (dense bitmap, `m` words),
S (sorted array), R (run/RLE), W (WAH/EWAH fill-compressed), and the complement representation if in
scope (confirm against current code state before finalizing — PROBLEM_STATEMENT.md §3 lists 5
including Roaring as an external baseline, not a matrix cell; NARRATIVE.md §7b confirms 5 views:
BitmapView, ListView, RunView, EwahView, ComplementView). State the asymptotic cost of each cell
symbolically (Θ(m), Θ(|S|), Θ(r), etc.) — this is where every cost-model symbol used anywhere in
Results must first be defined, per NARRATIVE.md §5's "every equation... lives here and nowhere else"
rule.
**Quality/analysis:** none — this subsection is definitional, not procedural; no statistics apply.

### 4.2 The pairing matrix and its cells

**Orientation:** state once, precisely, the count reconciliation flagged in NARRATIVE.md §7b: the
paper reports ten non-Roaring cells of the symmetric representation matrix; the complement-vs-bitmap
strategy (C×B) is an eleventh, asymmetric strategy rather than a symmetric matrix cell, and Roaring
itself is never built as a matrix cell — it is the external baseline throughout. **State this once,
here, and use the resulting count consistently in every later section** (this is a cross-cutting
consistency requirement, not just a local one — flag to the drafter to grep the draft for "ten" and
"eleven" before submission).
**Procedure:** list the implemented cells with their source files (per NARRATIVE.md §7b:
`cell_bb.cpp`, `cell_bs.cpp`, `cell_br.cpp`, `cell_sparse.cpp`, `cell_wah.cpp`, `cell_comp.cpp`), and
for each asymmetric cell, the specific algorithmic strategy used (e.g., B×R via rank-index O(1)
per-run lookup — cross-reference §4.3 rather than re-deriving here).
**Quality/analysis:** state, in one sentence, that the variant reported for each cell in Results is
the one selected as best per corpus/density regime rather than a single fixed implementation.
**Do not itemize or count the losing variants developed along the way** — per NARRATIVE.md §2 and §6,
the per-cell optimization record belongs "at most into a single table that no one has to read, and
preferably into a blog post instead," and Methods should not be the place that reconstructs that
record in prose. If a pointer is needed at all, one clause naming the record as available
elsewhere (not itemized) suffices.

### 4.3 Zone-map and rank-index construction

**Orientation:** brief functional definition of each, since both are the paper's two concrete
work-avoidance mechanisms and a reader must know what they compute before the Procedure detail means
anything. Template pattern from the skill: "To decide how much of a dense row a pairing needs to
visit, we used a zone map, which..." / "To answer a run's contribution to a bitmap without touching
its bits, we used a rank (prefix-popcount) index, which..."
**Procedure:** zone map — bin width in bits (512-bit bins per PROBLEM_STATEMENT.md §2.6), storage
cost as a fraction of the bitmap (state the number as measured/sourced, tier-labelled), construction
pass (single forward pass, or free if columnar storage already carries block occupancy). Rank index —
construction algorithm (prefix-popcount, cite Jacobson/Clark/Vigna rank9 as the lineage per
NARRATIVE.md §7), space overhead as a fraction of the bitmap, and the O(1)-per-run contribution
formula (`rank_B(b) − rank_B(a)`, from PROBLEM_STATEMENT.md §4) — this formula's *only* appearance in
the paper should be here.
**Quality/analysis:** state the two known failure modes as procedural facts, not results: zone maps
applied unconditionally are a pessimization on uniform-density data (cite the mechanism — filtering
that filters nothing is pure overhead — not the ratio, which belongs in Results/R3); zone maps do not
generalize to already-work-proportional cells (B×S, B×R). State the guard/gate condition under which
each mechanism is applied, since that gate is itself part of "the procedure," not a finding.

### 4.4 Selection policies: per-pair, per-tile, probe-and-commit

**Orientation:** one sentence distinguishing "selection" (choosing which cell to run) from the
mechanisms in §4.3 (deciding how much of a chosen cell's work to skip) — these are easy to conflate
and Results (R3) depends on the reader keeping them separate.
**Procedure:** define each granularity operationally — per-pair (metadata consulted once per (i,j));
per-tile (decision hoisted across a pre-partitioned block of rows sharing one kernel — state the tile
size/partitioning rule); probe-and-commit (name it explicitly as the ε→0 special case of Micro
Adaptivity, cite Răducanu/Boncz/Żukowski 2013 here, in Methods, in addition to wherever Results
mentions it by name). State every exact threshold used by any policy (e.g., the 2% runtime budget,
any density/cardinality cutoffs) as a concrete number, not a description — this is exactly the
"exact thresholds" requirement in NARRATIVE.md §5's Methods line.
**Quality/analysis:** state how selection cost itself was measured (instrumentation approach — timed
separately from kernel execution, or inferred by differencing) and how "regret against the bucket
oracle" is computed (the oracle definition: best cell per bucket, chosen with full hindsight,
compared against each online policy's actual choice).

### 4.5 Corpora, provenance and loaders

**Orientation:** none needed — corpus description is inherently procedural.
**Procedure:** enumerate the corpus families and their provenance: the CRoaring-default
`real-roaring-datasets` corpora (name the census1881 validation check against the published
statistics from arXiv:1603.06549 as a loader-correctness fact, per RESEARCH_PLAN.md §15.1), the
larger modern graph corpora added beyond CRoaring's default set, and the two genomic corpora (UShER
SARS-CoV-2, gnomAD chr21) with their layout stated explicitly (variant-major vs haplotype-major, per
RESEARCH_PLAN.md §22.1 — this distinction must be defined here since R6/L2 depends on it). State
`_srt` variants as the row-sorted clustering-point pairing the original Roaring papers also report.
List every corpus once, by name, with universe and density — this table's contents feed Table 1 and
should not be duplicated in prose.
**Quality/analysis:** state the loader-correctness check for at least one corpus (census1881's
reproduced published statistics) as the paper's evidence that provenance was not just claimed but
verified.

### 4.6 Benchmark protocol and cache-residency statement

**Orientation:** none needed.
**Procedure:** state warmup procedure, iteration count, timing method (wall clock / cycle counter,
name which), and how ties/baseline fairness were enforced (AGENTS.md rule 9: same flags, same
alignment, same warmup for CRoaring as for Storm's own kernels — state this explicitly, it is a
credibility point a reviewer will check first). State the working-set-size sweep protocol used by
R2/Fig. 3 (how "working set size" was varied and how L1/L2/DRAM boundaries were identified per host).
**Quality/analysis — cache residency, mandatory per AGENTS.md rule 5 and CLAUDE.md:** state cache
sizes for every host used (L1d, L2, and shared/SLC or L3 as applicable) and which regime each
reported throughput number sits in. This is not optional framing — CLAUDE.md explicitly flags the
legacy README's past failure to state this, and Methods must not repeat it. Every number that later
appears without an explicit residency qualifier in Results must have its qualifier defined once,
here, by regime name (e.g., "L2-resident," "DRAM-resident") so Results can use the regime name as
shorthand.

### 4.7 Statistics and aggregation

**Orientation:** none needed.
**Procedure:** state repeat count per measurement, aggregation statistic used for headline numbers
(median, per L6's explicit instruction to never quote a single run or a two-significant-figure
per-corpus ratio), and how ranges are computed (min–max across repeats, or across hosts, stated
separately — do not conflate a cross-host range with a cross-repeat range in the same sentence
anywhere in the paper).
**Quality/analysis:** state the measured run-to-run variance explicitly for at least the corpora
where it is largest (per RESEARCH_PLAN.md's prior finding that some corpora showed the *winning
cell itself* changing between repeats on 5 of 17 corpora) — this is a reproducibility disclosure, not
a hedge, and belongs in Methods precisely so Results/Table 1 can report clean ranges without
re-explaining the variance every time. State the criterion used to exclude any corpus as
"unquotable" (if the re-measurement reproduces that finding) and name it/them here so Table 1's
footnote can simply cross-reference this subsection.

### 4.8 Correctness oracle and differential testing

**Orientation:** none needed.
**Procedure:** describe the independent scalar oracle (`oracle.cpp` per NARRATIVE.md §7b) and the
differential-testing harness — every cell variant checked against it. State the check counts as
concrete numbers with zero framed as the result, not an aside: unit/ABI-level checks
(`test_storm.c`), cell-level differential checks (`test_cells`), and the larger cross-ISA differential
count run during the campaign. State the `nm`-verified ABI check (unmangled `STORM_` exports, zero
mangled symbols) as the C-ABI correctness statement.
**Quality/analysis:** state the fuzz/property-testing axes if applicable (`{n_vec, n_words, density,
clustering, alignment}` per AGENTS.md's PR checklist) and that a regression test's ability to fail
was itself verified by reverting each of the Phase 0 defects and confirming the suite went red (state
this as the paper's testing-of-the-tests discipline — it is unusual enough to be worth one sentence).

### 4.9 Hardware and toolchain

**Orientation:** none needed.
**Procedure:** list every host/microarchitecture used across the paper (four for the cross-ISA
synthetic comparison per NARRATIVE.md §4 B1, one — Apple M4 — for the real-corpus run per L5), each
with core/cluster identification, cache hierarchy sizes (cross-reference §4.6 rather than repeating),
compiler and flags (`-march=native` default per AGENTS.md conventions, and the
`-DSTORM_DISABLE_NATIVE` alternative if relevant to any reported number), and the exact ISA paths
exercised. **State the excluded-claims boundary explicitly and by name, per NARRATIVE.md §4
"Excluded" row:** no SVE/SVE2-specific intrinsic path exists — Graviton-class hosts run the generic
NEON path on SVE2-capable hardware, and this must be stated as fact here so no later section can be
misread as claiming an SVE2 kernel. Same treatment for AVX2/SSE4.2 — the CMake options exist but gate
only the legacy `libalgebra` path, not any pairing-matrix kernel.
**Quality/analysis:** state CRoaring's pinned version (v4.7.2) and vendoring path
(`third_party/croaring_amalg`) as the baseline's toolchain identity — a reader must be able to
reproduce the exact baseline, not just "CRoaring."

### 4.10 Data and code availability

**Orientation:** none needed.
**Procedure:** state repository location, license (Apache-2.0 per AGENTS.md), and the precise
artifact-status statement consistent with Discussion P5 and NARRATIVE.md §8's option (a): the
benchmark code and kernels are available; the pairing matrix, gate and tile pipeline are not
currently exposed through the library's public C ABI, and this is stated as a fact about the present
release, not apologized for. State how real corpora can be obtained (public sources, loader scripts)
and whether raw per-corpus timing data is deposited (Supplementary per NARRATIVE.md §6's inventory:
"per-corpus raw timings").
**Quality/analysis:** none — this subsection is a statement of fact, not a procedure with a quality
dimension.

---

### Reporting ratios — a rule that is not optional

**Never write "$N\times$ less work".** Read literally it is a subtraction, and it is routinely
misparsed by a factor of 100 when percentages are also in play. This paper reports magnitudes
spanning $8\times$ to $512\times$, where that misreading changes the claim materially --- it has
already happened once in drafting.

Use one of exactly two forms, and be consistent within a subsection:

| Form | Example | Use when |
|---|---|---|
| **cost as a fraction of the baseline** | "costs $0.195\%$ of a dense bitmap" | stating where a curve sits |
| **baseline as a multiple** | "a dense bitmap does $512\times$ the work" | stating an advantage |

Both are unambiguous and they convert cleanly ($0.195\% \leftrightarrow 512\times$). If a percentage
must be given for an advantage, give it as a percentage \emph{of the baseline} and state the
direction: $512\times$ is $51{,}200\%$, and $7.9\times$ is $790\%$ --- the two differ by two orders
of magnitude and a reader will not recover the intended one from context.

The same rule applies to figure captions, table headers and axis labels: an axis reading
"cost $\div$ dense-bitmap cost" is unambiguous; one reading "speedup" is not, unless the caption
says against what.

## 5. Tense-and-voice sheet and quantitative-reporting sheet

### 5.1 Tense and voice, by section

| Section | Default tense | Notes |
|---|---|---|
| Introduction | present, for stable facts about the field and about Roaring/popcount as they stand today; present for the two framing questions | Do not use past tense for "Roaring dispatches per chunk" — that is a stable property of the system today, not a one-time study action |
| Results | past tense for every study-specific action ("we measured," "the sweep showed"); present tense only for a stable definition restated inline ("a cell is a representation pairing") | Every Finding sentence should read as a completed measurement. Never present-tense a ratio ("B×B is 70× slower") — past-tense it ("was") or attribute it to the figure |
| Methods | past tense for study-specific procedure; present tense for stable properties of an algorithm ("the rank index returns a prefix count in O(1)") | Mirrors the skill's wet-lab convention exactly — "images were acquired" ⇔ "the corpus was benchmarked"; "the algorithm returns" ⇔ "the rank index returns" |
| Discussion | present tense throughout for interpretation ("the results indicate," "the pattern suggests"); past tense only when pointing back at a specific Results action | Standard for Discussion register |

**Voice:** functional first person ("we measured," "we compared," "we built") for every accountable
study action, exactly as the skill specifies. Reserve passive voice for steps where the actor is
irrelevant (e.g., corpus preprocessing steps common to all inputs). Never use first person for
persuasion ("we believe our method is superior") — let the ratio and the corpus count carry that
weight, per the skill's "matter-of-fact claims" rule and per AGENTS.md's baseline-fairness ethos.

### 5.2 Quantitative reporting rules, specific to this paper

**Speedups — ratio, not percentage, and never alone.** Report every speedup as an `×` ratio
(`5.4×`), never as a percentage improvement, to match the register of both Roaring papers and the
rest of this literature. A percentage is acceptable only for *cost* fractions that are themselves
percentages of a budget (selection cost as % of runtime, against the explicit 2% budget) — do not
mix the two conventions within one sentence.

**Ranges across hosts and corpora — always both bounds, never a single midpoint presented as
representative.** When a number spans hosts, state it as `[NUM: low]–[NUM: high]×` and name what
varies across the range (host, corpus, or both) in the same clause — "1.09–15.83× across 17 corpora"
is well-formed; "about 5×" is not, per NARRATIVE.md §9 rule 1 ("never quote a single headline
speedup"). Prefer median alongside range when both are available (`range, median ≈ M×`), because L6
establishes that individual corpus ratios are not trustworthy to two significant figures — the range
and the median are the trustworthy quantities, not any single interior point.

**Never report a per-corpus ratio to two significant figures without the median/range framing
available.** This is L6, promoted to a hard reporting rule: if a specific corpus's number is quoted
in prose (e.g., in R5's container-census discussion), quote it as measured but do not present it as
more precise than the aggregation subsection (§4.7) establishes repeats support.

**Cache residency — state with every throughput number, by regime name, not by re-stating cache
sizes each time.** Once §4.6 defines regime names (L1-resident, L2-resident, DRAM-resident, etc.),
every Results throughput number should carry its regime as an adjective or a parenthetical
("DRAM-resident: ..."), never bare. A number with no residency qualifier anywhere nearby is a defect,
per AGENTS.md rule 5.

**Evidence-tier attribution — every performance number is tier 1/2/3, and tier determines what verb
is allowed.**

| Tier | What it is | Allowed verb / framing | Example |
|---|---|---|---|
| 1 — Recalled | From memory, not opened in any source | **Never appears as a claim anywhere in the paper.** If a number cannot be traced to tier 2 or 3, it is a placeholder, not a draft sentence | — |
| 2 — Sourced | Published/vendor figure, or read directly from a competitor's source code | Attribute explicitly to its source and frame as *their* figure, not a property of Storm's own kernel: "CRoaring's container-selection threshold is 4096, per its source" | The CRoaring fixed-threshold value in R5; any Intel/ACLE-published latency number if one appears |
| 3 — Measured | Produced by this project's own benchmark on stated hardware | The only tier that supports a claim about *our* kernel's speed. State as a direct measurement: "the best cell measured 1.6 ns/pair" | Every ratio in Results Tables 1–2 and Figs. 2–4 |

**Rule of composition:** a tier-2 number and a tier-3 number must never be combined into a single
derived ratio presented as tier 3. If a sentence needs both (e.g., "CRoaring's threshold is 4096
[tier 2], and at that threshold our measured cost is X [tier 3]"), keep them as two clauses with two
explicit attributions, not one blended number.

**Oracle numbers and selection-cost numbers never compose into an implied end-to-end figure (L7).**
Any sentence citing both a best-cell (oracle) speedup and a selection-cost percentage in the same
breath must state explicitly that they were measured separately and do not multiply into a validated
end-to-end result — this applies most directly in R3 and must not silently recur in Discussion P1's
compressed restatement.

---

## 6. Claim-calibration table

Strongest wording the evidence shape supports, and the overclaimed wording to avoid, for every claim
carrying weight in the paper. "Strongest supportable" assumes the re-measurement reproduces the
*shape* recorded in NARRATIVE.md's claim-stability table (§4 there) — if a stable-shape claim does
not reproduce, that is itself a finding and this table's "strongest supportable" column must be
re-derived from the new measurement, not defended.

| Claim | Overclaimed wording — avoid | Strongest wording the evidence shape supports |
|---|---|---|
| SIMD vs. asymmetric cells (A1) | "SIMD vectorization does not help set intersection" / "vectorization is the wrong approach for this problem" | "A SIMD-throughput kernel won none of the 25 asymmetric measurement points tested; vectorization remained the best strategy only on the one cell where the work is not reducible, bitmap×bitmap" |
| Density U-shape (A2) | "Performance depends only on sparsity" | "Cost is U-shaped in density; the operative variable is distance from density one-half, not sparsity, and the sparse-tail and dense-tail mechanisms are symmetric" |
| Adaptive pairing vs. tuned CRoaring (A3) | "Storm is Nx faster than Roaring" (bare, corpus-unscoped) | "Adaptive pairing beat a `run_optimize`-tuned CRoaring baseline on every one of 17 real corpora tested, [NUM range]×, median ≈[NUM]×" — always attach the corpus set and the tuning statement |
| Winning-cell migration (A4) | "The best representation is easy to predict from density alone" | "The winning cell migrated across real corpora and no single cell dominated more than [NUM] of 17 corpora — the map, not any one ratio, is the result" |
| Selection cost (A5) | "Selection is free" / "selection overhead is negligible" | "Per-pair selection failed a 2% runtime budget; hoisting the decision to tile granularity or to a probe-and-commit policy brought it under budget by roughly an order of magnitude" |
| Measured vs. modelled selection (A6) | "Our selector is close to optimal" | "Measured (probe-and-commit) selection outperformed a closed-form cost model; the model alone performed worse than applying no selection at all" |
| Rank index and run length (A7) | "The rank index removes run-length as a cost factor" (stated with no crossover) | "A rank index made B×R cost proportional to run count rather than run length above roughly [NUM]-bit runs; below that crossover the index's own overhead dominates" |
| Vectorization decay / avoidance hold (A8) | "Work avoidance always wins more as data gets larger, on every architecture" | "Vectorization's advantage decayed with working-set size on every microarchitecture tested; work avoidance held or grew over the same axis, with the growth measured clearly on [N of 3] hosts" — do not assert the growth half beyond the hosts where it reproduces |
| Gating work avoidance (A9) | "Work avoidance is strictly beneficial" | "Applying the filter unconditionally was a net loss on this corpus set (geomean below 1.0×, [NUM] of 25 corpora harmed); gating it against a cheap online test eliminated every regression measured" |
| Storage liability (A10) | "Storm compresses better than Roaring" | "A dense bitmap over a large sparse universe cost 10³–10⁶ bits/value; representation selection avoids paying that cost, which is a storage consequence of the same skew that motivates the speed result, not an independent compression claim" |
| Cross-ISA synthetic result (B1) | Treated as equal-strength evidence to the real-corpus result (A3) | Always labelled "on synthetic data, four hosts" in the same sentence — never let a reader infer this is the real-corpus figure |
| vs. all-bitmap (B2) | Presented as the paper's headline number | Presented explicitly as the *secondary* baseline, with one sentence stating that improving Storm's own dense kernel shrank this column, which is a methodological point in the paper's favor, not a weakening of it (per NARRATIVE.md §9.3) |
| Complement representation as the designed dense-tail strategy (B3) | "The complement cell is a fast new technique" | "Computing on the complement above density one-half is a statable, selectable strategy that makes the dense tail a deliberate arm of the density sweep rather than an accident of R×R; if the final campaign sweeps it deliberately (C×B, C×C as first-class competitors — NARRATIVE.md §10, Gap 1), state the win over the previously-incidental R×R modestly, roughly [NUM]%. If the dense-tail sweep does not land as designed in time for drafting, keep this claim secondary and scoped exactly as it is here — do not promote it to primary standing without the designed sweep behind it" |
| Selection maturity (L1) | "Selection is a solved problem" | "Selection is no longer harmful, but nothing measured landed within roughly 1.5× of the oracle — selection remains an open margin, not a solved one" |
| Genomics / layout dependence (L2) | "The method works well on genomic data" (unscoped) | "Genomic data illustrates that the same underlying skew can present as sparsity or as a large universe depending on layout, rarely both at once; the haplotype-major real-corpus point measured below 1.0× — all-bitmap correctly wins there — while variant-major points measured [NUM range]×. The layout determines the regime" |
| Metadata liability (L3) | Omitted, or framed as a minor implementation detail | "Selector metadata is universe-proportional and currently unguarded; on the largest-universe sparse corpus measured, metadata outweighed data by roughly [NUM]×. This is a known, unfixed defect that currently blocks a deployable artifact" |
| Threading (L4) | Silence, letting a reader assume threading was evaluated | "All measurements are single-threaded; threading is out of scope for this paper by construction" |
| Cross-ISA real-corpus scope (L5) | Implying the 17-corpus result is cross-architecture | "The real-corpus comparison (R4) was run on one microarchitecture; the four-host comparison (B1) is synthetic. These are not the same evidence and must not be conflated in a single sentence" |
| Precision (L6) | Any bare two-significant-figure per-corpus ratio in prose or in Table 1 without a repeats/median footnote | Ranges and medians only; any corpus whose repeats disagree beyond the paper's own stated tolerance is named and excluded from headline claims, with the exclusion stated, not silent |
| Oracle vs. selector composition (L7) | A sentence that lets a CRoaring-comparison ratio and a selection-cost percentage appear to compose into one end-to-end number | Explicit statement wherever both appear together: the two were measured separately and do not compose into a validated end-to-end result |
| Tile-pipeline scope (L8) | "The method handles all-pairs computation end-to-end" | "The tile pipeline was measured on within-tile (block-diagonal) pairs; cross-tile blocks were not evaluated, and transpose-build cost is excluded from the reported numbers by a stated amortization argument" |
| Artifact / library status (NARRATIVE.md §8) | "We release Storm, a library for..." / "our library provides..." | "The pairing matrix, gate and tile pipeline described here are not currently exposed through the project's public C ABI; this paper reports a systematic measurement of the underlying mechanism, and the artifact work is sequenced to follow" — never "library," never "we release," in connection with the pairing-matrix work specifically |
| C11 / Roaring container mispricing (R5) | "Roaring's container selection is broken" / "Roaring is wrong" | "Roaring's fixed per-chunk threshold, chosen once at ingest for a single pairwise operation, can misprice global density at large-universe scale — observed directly via container census, not inferred" **and this claim must not appear in the paper at all unless re-verified against current CRoaring `master` first**, per NARRATIVE.md §3's explicit flag |

**Cut from the paper entirely — do not calibrate wording for these, exclude them outright.** Per the
author's explicit correction, none of the following justifies a design decision that survives into
the final system, which is NARRATIVE.md §2's sole test for inclusion: Harley-Seal/CSA accumulation,
naive run-collapsing, register blocking, prefetch behavior, hashed Bloom-filter prefiltering,
dual-tree/hierarchical row structures, the individually losing per-cell variants, and the
port-pressure/issue-width investigation (including its refutation — stating that a hypothesis was
tested and refuted is still spending a sentence on a discarded investigation). All of this belongs in
a blog post or, at most, one Supplementary table nobody has to read. **The phrase "negative result"
must not appear anywhere in the manuscript.**

---

## Summary of structural decisions and tensions found (for the orchestrator)

**Note on the correction applied mid-task.** An earlier draft of this document built a full Results
subsection and a Discussion paragraph around the project's negative results (refuted optimizations,
losing variants, the port-pressure investigation). The author rejected that framing explicitly and
NARRATIVE.md was revised accordingly (its §2 now states the inclusion test in exactly these terms:
"does removing it leave a design decision unjustified?"). This document has been rewritten to match:
R2 is now framed as a positive mechanism claim ("avoiding work beats accelerating it"), R3's gating
finding is framed as the positive justification for conditional filtering, R6 is scope statements
only, and Discussion no longer carries the genomic-correction-history paragraph. Everything below
reflects that corrected state, not the original draft.

1. **Discussion paragraph-slot reconciliation.** The skill's default Discussion architecture has six
   functional slots; NARRATIVE.md §2b's closing paragraph gives five specific content beats in a 4–5
   paragraph budget. Resolved by merging "central contribution" into a two-sentence opening and
   merging "limitations / future work / closing implication" into one short final paragraph (P5), per
   §2b's own instruction that this close be "short and unapologetic" rather than a second limitations
   section. Flagging this because if the drafter follows the skill's six-paragraph default literally,
   Discussion will overrun the budget.

2. **The skill's "Discussion introduces no new results" rule vs. NARRATIVE.md's "material absent from
   Results" instruction.** These sound contradictory but are not — resolved explicitly in §3 above:
   the work-avoidance/memory-hierarchy paragraph (P3) reuses R2's already-reported measurement and
   adds only forward-looking *interpretation*. Worth flagging to the drafter explicitly, because the
   surface wording could be misread as license to introduce new data in Discussion, which the skill
   forbids.

3. **NARRATIVE.md's own internal tension over publication strategy (its §8), inherited here.**
   RESEARCH_PLAN.md §16 explicitly *supersedes* §14 and argues the paper should be "a library with a
   paper" (composition claimed as a shipped system, end-to-end numbers with build cost charged) —
   this is option (b) in NARRATIVE.md §8. NARRATIVE.md's own recommendation is option (a) — publish as
   an empirical study now, artifact work sequenced after — explicitly because the author has not yet
   decided. This document has written every Methods/Discussion instruction under option (a) (no
   "library," no "we release," artifact gap stated in Discussion P5), consistent with NARRATIVE.md's
   own recommendation. **If the author instead chooses option (b) before drafting, the
   claim-calibration table's "Artifact / library status" row and Methods §4.10 both need to flip**,
   and R4/R5's framing of "vs. tuned CRoaring" would need revisiting to decide whether end-to-end
   (build-cost-charged) numbers replace the current block-diagonal/amortized ones as the headline,
   per RESEARCH_PLAN.md §16.3 and NARRATIVE.md §10's Gap 4 (below). This is the single highest-leverage
   open decision in the whole spec and is not this document's to resolve.

4. **NARRATIVE.md §10 (new since the correction) adds a "Gap register" the final campaign must close,
   and Gap 4 is a direct extension of tension 3 above.** Gap 4 states plainly that no number in the
   current evidence is end-to-end: every CRoaring ratio uses a best-cell oracle (Table 1 as currently
   specified in this document) and every selection-cost number comes from a separate harness. Its
   proposed fix — one table where the *deployed selector*, running end to end, beats tuned CRoaring
   with selection cost and build cost charged — would, if it lands, likely **become or replace Table
   1** rather than sit alongside it. This document has specified Table 1 and R4 on the oracle-based
   evidence described in NARRATIVE.md's existing claim ledger (A3, A4), consistent with L7's guard
   against composing oracle and selection-cost numbers. **RESOLVED by NARRATIVE.md §12 ("Two
   structural decisions the gap register forced").** When the end-to-end number lands it *becomes*
   Table 1 rather than sitting alongside it: the deployed selector's ratio against tuned CRoaring is
   the headline column with selection and build costs charged; the best-cell oracle returns as a
   second column explicitly labelled an upper bound; and the gap between the two columns is the
   per-corpus regret. R3 and R4 therefore share a spine — R3 establishes what deciding costs, R4
   shows what deciding is worth and how much remains on the table — and R4 reports one honest ratio
   beside a labelled bound, rather than an oracle ratio the reader must mentally compose with a
   selection cost taken from elsewhere. If the harness does not land in time, Table 1 stays
   oracle-only with "oracle" in the column header rather than in a footnote, and the Discussion names
   deployed end-to-end evaluation as the immediate next step.

5. **NARRATIVE.md §10's Gap 3 proposes a new display item that is not reconciled with the fixed
   six-item budget.** Gap 3 asks for a "cell × corpus-shape matrix, coloured by winning strategy
   class" to give the work-avoidance claim (R2) a map rather than a count in prose. NARRATIVE.md §6
   still lists exactly six main items (Figs. 1–4, Tables 1–2). **RESOLVED by NARRATIVE.md §7 and
   §12: by reassignment, not by a seventh item.** Fig. 3 panel (a) *is* the strategy map — cell ×
   corpus shape, coloured by which strategy class wins there — and the residency curves move to
   panel (b). R2 must be specified against that assignment: its central finding is carried by a map,
   not by a count in prose. The display budget stays at six.

6. **NARRATIVE.md §10's Gap 1 (the dense tail is the weaker arm of the U) directly affects R1's
   Finding 4 and the calibration-table row for the complement representation.** R1 is specified above
   to describe the dense-tail win via the complement strategy as if it is already a designed arm of
   the sweep; if the final campaign has not yet made C×B/C×C first-class competitors by drafting time,
   R1's Finding 4 and the "Complement representation" calibration-table row both fall back to the
   more modest, previously-secondary framing (a win over an *incidental* R×R result, not a designed
   one) — both places in this document already carry that contingency note, but it is worth
   surfacing here as a single point: **R1's dense-tail claim and the complement-representation
   claim-calibration row are coupled and must be drafted consistently with each other**, whichever
   framing the final campaign supports.

7. **C11 (R5's entire subsection) is gated on a re-verification NARRATIVE.md itself flags as
   outstanding.** This document has written R5's full content spec on the assumption it reproduces
   but has marked, in three places (R5's header, its Approach, and the claim-calibration table), that
   it must not be drafted into final prose until re-checked against current CRoaring `master`. If it
   does not reproduce, R5 needs a different mechanism-level finding or must be folded into R4 as a
   shorter observation — that would be a level-1 decision, not a rewrite of this document.

8. **No tension found** between the skill's R-A-F-I model and NARRATIVE.md's separating-axis table —
   they compose cleanly; the separating axis becomes each subsection's Reason-paragraph framing
   device, which is exactly the role NARRATIVE.md already assigns it ("named in its opening
   sentence").

9. **Results lead-in paragraph** is required by the skill (2–5 sentences before the first
   subsection) but not explicitly reserved as a line item in NARRATIVE.md's word-budget table. Added
   here as an explicit ~60–90 word allowance carved out of the Results total, not additive to it.
