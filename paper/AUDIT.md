# Manuscript audit — `storm.tex` and `sections/*.tex`

Standard applied: `manuscript-architecture` (level 1) then `paper-figures`. Authorities read:
`NARRATIVE.md` (thesis, skeleton, claim ledger, §7 display inventory, §12 gap register + the two
settled structural decisions), `SECTION_SPEC.md`, `DISPLAY_ITEMS.md`.

Build state after the pass: **compiles, 12 pages, `check_display_items.py` PASS (13 labels,
29 references)**. The only remaining layout warning is a pre-existing 0.55 pt `Overfull \hbox` on a
`\texttt` filename list in Methods (`methods.tex:50–61`), unchanged by this pass and below the
threshold worth trading for loose interword spacing. Both table overflows are gone. Undefined-citation
warnings remain and are expected — `references.bib` is owned by another agent and was not touched.

**Scope note.** The all-pairs → set-algebra reframing landed mid-audit. Per the coordinator, this
pass stayed mechanical: referential integrity, headings, notation, terminology, forward references,
bare numbers, placement, captions, table typography. **No section was rewritten into the new
framing.** §9 below is the file-and-line inventory of prose that is specifically committed to the
retired all-pairs frame, handed over for the reframing pass.

**Concurrency warning.** `storm.tex`, `sections/methods.tex` and `sections/results_b.tex` were each
modified on disk by another agent *during* this audit (cite-key normalisation, `\subsection*`
starring in `results_b`, `\spec{}` wrapping in `results_b`, and the title change). All edits below
were applied with exact-match replacement and re-verified against a clean rebuild, but a
line-number diff against any snapshot older than this file will not line up.

---

## Summary by class

| Class | Found | Fixed | Escalated |
|---|---|---|---|
| 1. Referential integrity | 7 | 7 | 0 |
| 2. Heading / notation / terminology | 9 | 9 | 0 |
| 3. Disjointness and duplication | 6 | 6 | 0 |
| 4. Forward-reference discipline | 10 | 10 | 0 |
| 5. Placement (Methods-only material outside Methods) | 5 | 5 | 0 |
| 6. Display items and table typography | 11 | 10 | 1 |
| 7. Abstract audit | 3 | 3 | 0 |
| 8. Bare numbers outside `\nm{}` | 4 | 4 | 0 |
| **Total** | **55** | **54** | **1** |

Plus **4 decisions listed in §10 that the orchestrator should ratify or overturn**, and the §9
reframing inventory (28 sites, not defects — handover).

---

## 1. Referential integrity

`scripts/check_display_items.py` was run over `storm.tex` + all six section files. It reported PASS
both before and after; every defect below is one the script *cannot* see and that was found by
reading. It now passes with 10 labels and 29 references (was 12).

| # | Violation | Where | Action |
|---|---|---|---|
| 1.1 | **Fig. 1 had no narrative sentence.** Its only mention was a bare parenthetical inside a definitional clause (`…the full set of such cells (Fig. 1)`). The skill is explicit that a citation is not narration. | `results_a.tex` lead-in | FIXED — added a sentence stating what the figure shows (families, driving variable per pairing, metadata→selector→kernel path). |
| 1.2 | **Fig. 3 had one trailing parenthetical** at the end of a paragraph, not placed after the claim it supports, and no sentence saying what the figure shows. | `results_a.tex` R2 | FIXED — Fig. 3a now cited immediately after the strategy-class claim with a sentence describing the map; Fig. 3b cited at the residency claim. |
| 1.3 | **Table 1 first mention was a trailing parenthetical** after a ratio, with no sentence naming its columns. | `results_b.tex` R4 ¶2 | FIXED — opens with `Table~\ref{tab:corpora} reports, for every corpus, …`. |
| 1.4 | **Table 2's data provenance was narrated only in Results prose and its own caption**, never as a method. | `results_b.tex` R5 ¶1 | FIXED — R5 ¶1 now states what the table shows and points to the new Methods subsection. |
| 1.5 | **Hard-coded "Table 1" ×5 and "Table 2" ×1** instead of `\ref` — silently wrong the moment a float moves. | `methods.tex` (5), `results_b.tex` (1, inside a `\spec{}` block) | FIXED — all converted to `Table~\ref{…}`. |
| 1.6 | **A `\ref` inside a table body cell** (`\nm{dense corpus (as Table~\ref{tab:corpora})}` in Table 2's first column) — a table-to-table cross-reference the reader never enters from the argument. | `results_b.tex` Table 2 | FIXED — reduced to `\nm{corpus}`; the cross-reference now lives in the caption, where it belongs. |
| 1.7 | **`(Methods)` and `(Results)` cited as whole tiers**, not as discrete targets. The skill requires naming the subsection. | 9 sites across `methods.tex`, `discussion.tex`, `results_a.tex` | FIXED — see §4. |

`sec:r4`/`sec:r5`/`sec:r6` labels added to `results_b.tex` so all six Results subsections are
labelled symmetrically (`results_a.tex` already had `sec:r1`–`sec:r3`).

---

## 2. Heading, notation and terminology consistency

| # | Violation | Action |
|---|---|---|
| 2.1 | `results_b.tex` used unstarred `\subsection{…}` ×3 against `\subsection*{…}` everywhere else — would have produced numbered headings in an unnumbered skeleton. | FIXED (concurrently also fixed by another agent; verified). All 20 headings are now `\section*`/`\subsection*`. Discussion has zero subheadings, as the venue requires. |
| 2.2 | **`\texttimes` vs `$\times$`** — `results_b.tex` used the text macro for ratio symbols, `results_a.tex` used math mode. | FIXED — normalised to `$\times$` throughout; zero `\texttimes` remain. |
| 2.3 | **Palette contradiction.** `storm.tex`'s preamble comment claims it is "ONE definition for the whole paper (DISPLAY_ITEMS.md section 1.1)", but three of five colours disagreed with §1.1 (`B` #666666 vs #222222, `R` #EE6677 vs #228833, `W` #228833 vs #AA3377). `W` was set to the exact hue §1.1 assigns to `R` — a guaranteed semantic collision the moment figures are generated. | FIXED — aligned to §1.1 and added the reserved `reprRef` #BBBBBB ("competitor / trivial baseline"). |
| 2.4 | **"kernel" used to mean "cell".** Fig. 1's caption said the five families "form *N* pairwise **kernels**", contradicting the lead-in's own definition of *cell*. | FIXED — "cells". |
| 2.5 | **"winning kernel" vs "winning variant".** R2 said "the winning kernel at every measurement point", where Methods defines *variant* as the implementation level. | FIXED — "winning variant" in R2 (×2), matching Fig. 2c's caption and Methods. |
| 2.6 | **`\emph{variant}` was used in Results and Methods but never defined.** | FIXED — defined once in the Results lead-in alongside *cell* and *pairing matrix*. |
| 2.7 | **"the selection gate" vs "the selector".** Two names for one component across Methods/Discussion. | FIXED — normalised to "the selector". |
| 2.8 | **Two different oracles both called "the oracle."** Table 1's upper-bound column is a *best-cell* (per-corpus, hindsight) oracle; R3/Fig. 4b's denominator is a *bucket* oracle (per-shape-bucket). Table 1's caption and R4's prose both asserted the deployed–oracle gap was "regret against the **bucket** oracle", which is a different quantity and not even a bound on it. | FIXED — see 6.5; Methods now defines both and states they are never interchanged. |
| 2.9 | **`Density` (Table 1) vs `Global density` (Table 2)** used for different quantities without either being defined, against `NARRATIVE.md` Reconciliation #5 ("characterise every corpus by its median density, never its mean"). | FIXED — Table 1 header is now `Median density`; Table 2 keeps `Global density` and its caption defines it. |

---

## 3. Disjointness and overlap

Disjointness test run on all 15 subsection pairs. All six axes are stateable in ≤8 words and all six
opening sentences name them, so **no re-cut is required** — the skeleton is sound. The defects were
repeated *content*, not bad cuts.

| Pair | Separating axis | Verdict |
|---|---|---|
| R1↔R2 | strategy compared, not data | OK |
| R2↔R3 | cost of deciding, not computing | OK |
| R3↔R4 | real data, not synthetic | OK |
| R4↔R5 | competitor's mechanism, not ours | OK |
| R5↔R6 | scope boundaries, not evidence | OK |
| R1↔R6 | R6 restates R1's band as scope | OK (concessive bridge, sanctioned) |

| # | Duplication | Action |
|---|---|---|
| 3.1 | **The residual-regret statistic appeared three times at the same specificity** — R3 (`\nm{residual factor…}`), R6 (`\nm{closest measured regret…}`), Discussion (`\nm{selector-to-bucket-oracle regret bound…}`). `SECTION_SPEC.md` R6 explicitly says "do not re-derive the regret numbers here, one clause pointing back to R3 suffices." | FIXED — quantified once, in R3, where Fig. 4b sits. R6 and Discussion are now one-clause anaphoric pointers with no numeric slot. |
| 3.2 | **The 4096 container threshold and its source coordinates appeared in four places** — Introduction, R5 prose, Table 2 caption, and (newly) Methods. | FIXED — Introduction now qualitative; R5 prose says "a compile-time constant" and points to Methods; the caption keeps the mechanism but not the file:line; the coordinates live in Methods only. |
| 3.3 | **CRoaring's version pin + vendor path duplicated three times** (R5 prose, Table 2 caption, Methods "Hardware and toolchain"). | FIXED — Results and the caption defer to Methods. |
| 3.4 | **`roaring_bitmap_statistics()` + "taken after `run_optimize()`" stated at identical detail in R5 prose and Table 2's caption.** | FIXED — the API call and field list are Methods-only; Results and caption carry only the fact that the census is post-tuning. |
| 3.5 | **The selection-cost budget was stated twice inside R3**, both as `\nm{}` slots, one sentence apart. | FIXED — stated once with an explicit Methods handoff. |
| 3.6 | **Introduction pre-empted R5's claim.** ¶4 carried the container-threshold forensics (`DEFAULT_MAX_SIZE`, `roaring.h:2486`, `roaring.h:5515`, `roaring.c:10901`) — that *is* R5's evidence, delivered before any claim had been made. | FIXED — Introduction states the fixed-granularity commitment qualitatively; the evidence stays in R5/Table 2. |

Legitimate repetition left alone: probe-and-commit's one-clause description in R3 vs its
parameterised spec in Methods (correct tier layering, each strictly adds); the Discussion's opening
thesis restatement; R6 restating R1's mid-band as a scope boundary.

---

## 4. Forward-reference discipline

| # | Violation | Action |
|---|---|---|
| 4.1 | **Three `(below)` tags in the Results lead-in** — explicit forward pointers into unread Results. | FIXED — the sequence sentence stays (SECTION_SPEC requires it); the `(below)` markers are gone. |
| 4.2 | **`(Results)` ×3 in Discussion** — the skill records *zero* body-text instances of "see Results"/"see Discussion" across 13 papers. | FIXED — replaced with the specific backward targets: `Table~\ref{tab:census}`, `Fig.~\ref{fig:strategy-decay}a`, `Fig.~\ref{fig:selection-cost}b`. |
| 4.3 | **`(Results)` ×5 in Methods**, plus one bare "Results depends on keeping the two separate". | FIXED — replaced with `Fig.~\ref{fig:selection-cost}c`, `Figs.~\ref{fig:density-sweep} and~\ref{fig:strategy-decay}`, `Fig.~\ref{fig:strategy-decay}b`, `Table~\ref{tab:corpora}`; the bare sentence rewritten. |
| 4.4 | **`(Methods)` cited as a whole tier** in Discussion ¶2 and R1/R3. | FIXED — now names the subsection, e.g. `(Methods, ``Zone-map and rank-index construction'')`. |
| 4.5 | **R3's method paragraph trailed off** with no handoff at the Results→Methods seam. | FIXED — ends `See Methods, ``Selection policies: per-pair, per-tile, and probe-and-commit,'' for the budget, the tile geometry and the probe count.` |

The Introduction's closing `Here we…` roadmap is present, spent exactly once, and previews findings
in delivery order. It is the only forward-looking device left in the manuscript. Retained.

Remaining, and judged acceptable: R1's `…the axis against which every later comparison in this
section is implicitly read` — structural framing of an axis, not a preview of an unread claim, and
`SECTION_SPEC.md` R1 explicitly assigns R1 that job.

---

## 5. Placement

| # | Violation | Action |
|---|---|---|
| 5.1 | **Vendor source coordinates in the Introduction and in Results** (`roaring.h:2486`, `roaring.h:5515`, `roaring.c:10901`, `third_party/croaring_amalg`, `v4.7.2`) — reproduction-grade detail at Introduction/Results altitude. | FIXED — moved to Methods; the run-length pricing point (formerly Introduction) moved to Methods "Container census" where the rank index that removes it is specified. |
| 5.2 | **Table 2's data-acquisition procedure had no Methods home at all** — the API, the post-`run_optimize()` ordering and the recorded fields existed only in Results prose and a caption. | FIXED — new `\subsection*{Container census}` in Methods (between "Corpora, provenance and loaders" and "Benchmark protocol"). Results and the caption now point to it. |
| 5.3 | **Table 1's oracle column had no Methods definition**, while a *different* oracle did. | FIXED — new paragraph in Methods "Selection policies" defining both and stating the best-cell oracle is not a bound on the bucket oracle. |
| 5.4 | **The selection-cost budget threshold restated in Results.** | FIXED — one mention, needed to read "failed the budget", with a Methods pointer. The exact figure remains a Methods-only `\nm{}` slot. See §10.2. |
| 5.5 | **Two unslotted quantitative claims in R3** — "an order of magnitude beyond what the budget allows" and "roughly an order of magnitude of headroom". These are measurement claims in a manuscript whose measurements are all pending. | FIXED — first made qualitative ("well beyond what the budget allows"), second converted to `\nm{headroom of the hoisted policies against the budget, as a factor}`. |

No equations survive outside Methods. The inclusion–exclusion identity, the rank identity and the
per-cell asymptotic table are all Methods-only. Verified by sweep.

---

## 6. Display items

Inventory checked against `NARRATIVE.md` §7 as amended by §12. Six main items, correct: Fig. 1
schematic, Fig. 2 density sweep, Fig. 3 strategy map + residency, Fig. 4 selection cost/regret/
gating, Table 1 real corpora (deployed headline + oracle upper bound), Table 2 container census.

| # | Violation | Action |
|---|---|---|
| 6.1 | **Fig. 3 was built to the superseded spec.** Both panels were residency curves (SIMD decay in **a**, zone-map growth in **b**), i.e. `DISPLAY_ITEMS.md` §4. `NARRATIVE.md` §12's settled decision ("Gap 3 is resolved by reassignment") makes **3a the strategy map** — cell × corpus shape coloured by winning strategy class — and moves the residency curves to **3b**. As drafted, the paper's single most reusable artefact did not exist, and the abstract's lead mechanism clause had no display item. | FIXED — placeholder text and caption rebuilt: **a** is the strategy map; **b** carries both mechanisms against footprint as a minimal pair (line style = mechanism, colour = host, per `DISPLAY_ITEMS.md` §1.1's rule that hue is reserved for representation identity). |
| 6.2 | **Both tables set the caption *below* the tabular.** `paper-figures`: caption above, `\label` immediately after `\caption`. | FIXED, both tables. |
| 6.3 | **Table 1 overflowed the text block by 686 pt; Table 2 by 577 pt.** Long descriptive `\nm{}` strings inside `l`/`S` cells cannot wrap. This is a real failure under `paper-figures`, not a pre-existing warning. | FIXED — in-cell slots reduced to short tokens (the descriptive text belongs in the gap register, not a table cell), `S`-column cells braced, `\arraystretch` tightened to 1.05. Zero overfull warnings remain. |
| 6.4 | **Table 1's left rail was inconsistent** — `Sparse` used `\multirow{5}{*}{\rotatebox{90}{…}}`, `Mixed` and `Dense` were bare text in the rail column. Rendering `Mixed`/`Dense` rotated inside one-row groups overprinted them into an illegible glyph pile (verified on the rendered page). | FIXED — uniform unrotated `\multirow` rail. **Deviation from `DISPLAY_ITEMS.md` §6**, which specifies a rotated rail; rotation is unreadable at one-row group height and the table has horizontal room. See §10.1. |
| 6.5 | **Table 1's caption misidentified its own upper bound** as the bucket oracle (see 2.8). | FIXED — caption now says regret is against *that* upper bound; Methods §"Selection policies" distinguishes the two oracles explicitly. |
| 6.6 | **Table 1 lacked the group rail rule.** `DISPLAY_ITEMS.md` §8 specifies `|` at the rail boundary only. | FIXED — `@{}c \| l S S l S S@{}`. |
| 6.7 | **Missing-value token was inconsistent** — `--` in table bodies, `---` elsewhere, neither defined. | FIXED — `---` throughout, defined in both captions ("---, not applicable"). |
| 6.8 | **`Density` column format disagreed between tables** — `S[table-format=1.2e-1]` (Table 1) vs `1.1e-1` (Table 2). | FIXED — unified to `1.2e-1`. |
| 6.9 | **Table 1's `S` columns carried unbraced `\textbf{\nm{…}}`**, which siunitx must not parse as a number. | FIXED — all non-numeric cells braced. |
| 6.10 | **Table 1's `†` rows carried a dagger on a `\nm{ratio}` slot**, implying a value exists behind an excluded ratio. | FIXED — the excluded ratio cells now contain `†` alone; the dagger's meaning is stated in the caption. |
| 6.11 | **Table 2's caption declares a bold/plurality convention no cell uses.** | ESCALATED — correct to declare it now (the convention is real and the numbers are pending), but it will read as a broken promise if the campaign lands and nobody bolds the plurality cell. Flagging so it is not forgotten at fill-in time. |

Float placement: all four figures `[!t]`, both tables `[!t]`. Both tables were `table*` in a
single-column class (`wlscirep` never issues `\twocolumn`), which forced them onto float pages and
produced 12 pt and 11 pt overfull `\vbox` warnings; converted to `table`. Every float now appears on
or after the page of its first narrative mention. Rendered pages 2–6 were visually inspected.

---

## 7. Abstract audit

Clause by clause, against the main inventory.

| Clause | Backing item | Verdict |
|---|---|---|
| "normally done by … `popcount(A AND B)`" | — | background, no item needed |
| "costs Θ(m) … regardless of either set's cardinality" | Fig. 2a (flat B×B line) | OK |
| "State-of-the-art compressed bitmaps already adapt … adaptive in structure, but fixed in granularity" | Table 2 | OK, **strengthened** — the clause now states the observable consequence ("a corpus that is dense in aggregate can still receive almost no bitset containers") so it maps onto what Table 2 actually shows |
| "treating the representation pairing itself as a **per-pair, per-chunk** choice" | — | **VIOLATION, FIXED.** The paper's own R3 finding is that per-pair selection *fails* the budget and must be hoisted. The abstract advertised the granularity the results reject. Rewritten to "the object of choice … hoisted above the innermost loop." |
| "selected from measured rather than modelled properties" | Fig. 4b | OK |
| "wins none of [N] asymmetric measurement points" | Fig. 3a | OK **only after 6.1** — this clause had no display item before Fig. 3a was restored |
| "beats a tuned reference implementation on every one of [N] real corpora" | Table 1, Deployed column | OK |
| "at a selection cost of [X] of runtime" | Fig. 4a | OK |
| "no single representation pairing wins a plurality" | Table 1, Winning-cell column | **ADDED** — `DISPLAY_ITEMS.md` §6 calls this "the more important half of the claim" and the abstract omitted it |
| ~~"The result generalizes beyond this computation"~~ | none | **VIOLATION, FIXED.** An untested transfer claim, unhedged, with no display item anywhere — and the Discussion states the *same* claim inside `\spec{}`. An abstract cannot be less calibrated than the Discussion. Narrowed to the mechanism the measurements support; the transfer claim stays in Discussion under `\spec{}` where it belongs. |

The abstract cites no figure, table or Methods pointer. Correct.

**Consequence of the reframing, per the coordinator's point 5:** the abstract's first clause still
scopes the paper to *intersection cardinality*. Under the new frame it must claim the operation set
— and union, difference and symmetric difference are entirely unmeasured (`NARRATIVE.md` §12
[GAP 11]). The abstract currently makes **no** claim about the wider operation set, which is the
safe state. **Do not let the reframing pass add one until GAP 11 closes.**

---

## 8. Bare numbers outside `\nm{}`

Swept both digit forms and spelled-out forms across all six files.

| # | Site | Action |
|---|---|---|
| 8.1 | `discussion.tex` — "the synthetic cross-architecture comparison spans **four** microarchitectures", while `methods.tex` carries the same count as `\nm{… expected "four"}`. | FIXED — converted to `\nm{count of microarchitectures in the synthetic cross-architecture comparison}`. |
| 8.2 | `results_a.tex` R3 — "an order of magnitude beyond", "roughly an order of magnitude of headroom". | FIXED (see 5.5). |
| 8.3 | `introduction.tex` — "a compile-time constant of **4096** elements". | FIXED — removed from the Introduction; the value is now a `\nm{value of DEFAULT_MAX_SIZE in the pinned CRoaring version}` slot in Methods, since the pin is itself flagged as pending re-verification. |
| 8.4 | `methods.tex` — the cell count was `\nm{stated count …, expected "ten"}` in one sentence and the literal "ten"/"eleventh" in the three sentences after it, in the same paragraph. | FIXED — made literal "ten" throughout. Justification and escalation in §10.3. |

Reviewed and deliberately left as literals (none is a campaign output):

- Asymptotic and policy notation — `$N^2$`, `$O(1)$`, `$\varepsilon\to0$`, `$2^{16}$`.
- Reference-line values in captions — `$1.0\times$` (the no-benefit line).
- Structural design counts — "four representations", "three corpus families", "three selection
  granularities", "three levels" of correctness testing. These are decisions, not measurements.
- Vendor facts — "Roaring's three per-chunk containers"; "the twelve corpora CRoaring's own harness
  runs by default" (size of the external `real-roaring-datasets` collection, and the anchor the
  `\nm{total corpus count}` slot is built on).
- Dataset, ISA and licence identifiers — chr20/chr21, phase 3, AVX-512, x86-64, SSE4.2, Apache 2.0.
- `results_a.tex` R1's "99.99 % set … 0.01 % set" — an illustrative complement pair, definitional
  rather than measured. Flagged for a second opinion but not converted; converting it would make the
  sentence unreadable.

---

## 9. All-pairs framing inventory — handover for the reframing pass

Every site in the drafted prose that is *specifically* committed to the retired all-pairs /
batch / N² frame, or to intersection-cardinality as the only operation. Line numbers are as of this
file's writing; `results_b.tex` shifted by up to +3 lines when `sec:r4`–`sec:r6` labels were added.

### 9a. Frame-defining sentences — must be rewritten, not patched

| File:line | Text | Why it is frame-committed |
|---|---|---|
| `abstract.tex:1` | "Computing the intersection cardinality between every pair among $N$ sets --- the inner loop of similarity search, record linkage, and graph analytics" | The abstract's opening move *is* the all-pairs frame, and it also fixes the operation set to intersection cardinality. |
| `introduction.tex:1` | "Computing the intersection cardinality $\|X_i \cap X_j\|$ between every pair among $N$ sets is a core primitive…" | Same, for the Introduction's P1. |
| `introduction.tex:5–6` | "$N$ large enough that the all-pairs computation performs on the order of $N^2$ individual cardinality operations, so the **aggregate** cost of the primitive, not the cost of any single call to it, is what determines whether an analysis is tractable" | This is the load-bearing motivation for the whole paper and it is *purely* an N² argument. Under two-arbitrary-sets framing there is no aggregate to amortise over, and this paragraph has no replacement — it needs a new motivation, not an edit. **Highest-risk site in the manuscript.** |
| `introduction.tex:43` | "can the choice itself be made cheaply enough at $N^2$ scale that the saving survives the deciding?" | Framing question 2 (`NARRATIVE.md` §4's verbatim device) is stated in N² terms. If selection must be cheap for a *single* operation on two sets, the question is strictly harder and the answer may change. |
| `discussion.tex:3–4` | "in all-pairs set-intersection cardinality, choosing which representation pairing to evaluate --- cheaply, per pair or per tile…" | Discussion's thesis compression. |
| `discussion.tex:25–27` | "The **all-pairs batch setting is also new**. Roaring's per-chunk dispatch is designed around one pairwise operation, and at $N^2$ pairs the cost of making that decision … becomes a first-order question that a pairwise design has no occasion to ask." | This is the paper's stated *novelty claim against Roaring*, and it is entirely an N² claim. Under the new frame the paper is itself "designed around one pairwise operation", which inverts the argument. Needs a new differentiator. |
| `results_a.tex:4` | "We evaluated representation-pairing kernels for **all-pairs** set-intersection cardinality in three synthetic stages…" | Results lead-in scope sentence. |
| `methods.tex:98–99` | "applying the same test as an *exact* work-count for **all-pairs** intersection cardinality" | Zone-map novelty statement, scoped to all-pairs. |

### 9b. Batch-only assumptions embedded in the method

| File:line | Text | Issue under the new frame |
|---|---|---|
| `results_a.tex:152` | "selecting per pair -- a decision made at every $(i,j)$ --" | `(i,j)` indexing presumes an N×N matrix. |
| `results_a.tex:152–159`, `164–166`, `193` | The per-pair / per-tile / probe-and-commit comparison, and the conclusion that selection **must be hoisted to tile granularity** | **The single largest structural exposure.** Tile hoisting only exists because many pairs share a decision. For two arbitrary sets there is no tile, so R3's headline finding ("selection must be hoisted") has no meaning in the general case, and probe-and-commit — which amortises timing probes over a tile's remaining pairs — has nothing to amortise over. R3 either becomes a *special-case* subsection or needs a genuinely per-operation selection result that does not exist yet. |
| `methods.tex:102`, `108–118` | `\subsection*{Selection policies: per-pair, per-tile, and probe-and-commit}`; "rows are first partitioned into tiles of `\nm{tile width in rows}` rows"; "a tile commits to whichever cell was empirically fastest on that sample for its remaining pairs" | Same, at Methods altitude. The tile is a batch construct throughout. |
| `methods.tex:129` | "a closed-form **per-pair** cost model" | Fine as a name, but reads as batch vocabulary. |
| `discussion.tex:67–68` | "the tile pipeline has so far been evaluated on within-tile pairs only, with the cross-tile case and the transpose-build cost left for the end-to-end evaluation that follows" | A limitation stated *about the batch pipeline*. Under the new frame this limitation is either irrelevant or must be recast. |
| `discussion.tex:70`, `methods.tex:288` | "the pairing matrix, the selector and the tile pipeline described here are not currently reachable through the project's public C ABI" | Artifact-gap statement; names the tile pipeline as a headline component. |
| `results_b.tex:39–40`, `88` | "round-robin-interleaved repeats", "the current single-batch protocol" | Measurement protocol vocabulary that presumes a corpus-level batch. Cosmetic, but it signals the frame. |
| `results_a.tex:49`, `71` | "ns **per pair**", "Cost **per pair** against density" | Fig. 2's y-axis unit. Under the new frame the unit is per operation on an operand pair — probably survives a rename, but it must be renamed consistently or Fig. 2's axis will contradict the text. |
| `results_a.tex:27` | Fig. 1b caption: "per-pair metadata (cardinality, run count, chunk-occupancy bitmask), computed **once at build time**" | "Once at build time" presumes metadata is amortised across many operations on the same set. For a one-shot operation on two arbitrary sets, build cost is charged to that single operation and the selection-cost argument changes materially. **Flagging this as a substantive claim risk, not a wording issue.** |

### 9c. Operation-set commitments (intersection-cardinality only)

Relevant to the coordinator's point 5: every one of these narrows the paper to one operation, and
all of them are currently *true*, because nothing but intersection has been measured.

| File:line | Text |
|---|---|
| `methods.tex:5–7` | "each with a distinct asymptotic cost for computing an intersection *cardinality*, $\|X_i \cap X_j\|$, rather than the intersection itself" |
| `methods.tex:26–33` | The whole per-cell cost table is stated as intersection costs. Union/difference/symmetric-difference costs are **not** derived anywhere. |
| `methods.tex:44` | The inclusion–exclusion recovery $\|A \cap B\| = \|B\| - \|A^c \cap B\|$ — intersection-specific. |
| `methods.tex:240` | The correctness oracle "computes intersection cardinality" — the oracle itself does not cover the other operations. |
| `discussion.tex:22–24` | "the **cardinality-only setting** admits a shortcut that a materializing container format cannot take" — this is the paper's second differentiator against Roaring, and it is explicitly *cardinality-only*. A paper claiming full set algebra including materialised results cannot also claim this shortcut without heavy qualification. |
| `discussion.tex:30` | "evidence about a materialization-oriented format applied to a **cardinality-only query**" |
| `results_a.tex:4`, `abstract.tex:1`, `introduction.tex:1` | as in 9a |

**Consequence to weigh before the reframing pass runs:** two of the paper's three stated
differentiators against Roaring (`discussion.tex:22–24` cardinality-only rank shortcut;
`discussion.tex:25–27` all-pairs batch setting) are *dissolved* by the reframing. Broadening the
operation set to include materialising operations removes the first; dropping the batch removes the
second. What survives is the wider representation set (`\rW` + `\rC` beyond Roaring's three
containers). That may not be enough on its own, and it is a thesis-level question, not a drafting
one.

---

## 10. Decisions for the orchestrator

1. **Table 1's rail is unrotated** (6.4), against `DISPLAY_ITEMS.md` §6's rotated-rail spec.
   Rotation is illegible for the one-row `Mixed` and `Dense` groups and the table has horizontal
   room. If the final corpus set gives every band 3+ rows, revert to rotation for consistency with
   the spec. Verified on the rendered page either way.

2. **The selection-cost budget is named once in Results** (5.4). Strictly, the placement ladder puts
   every threshold in Methods only. But "per-pair selection failed the budget" is unreadable without
   knowing a budget exists, and `DISPLAY_ITEMS.md` §5 requires the budget drawn as a reference line
   in Fig. 4a — a caption must decode its own reference line. I kept one mention plus a Methods
   handoff; the numeric value stays a Methods-only slot. Overrule if you want zero Results mentions.

3. **"ten cells" is now a literal, not a slot** (8.4). The sentence enumerates all ten cells inline,
   so a `\nm{expected "ten"}` slot immediately followed by a list of exactly ten items reads as an
   error. It is a definitional count fixed by the representation set `{B,S,R,W}`, not a campaign
   output. If the reframing changes the representation set — which the set-algebra reframing plausibly
   does — this number and Fig. 1's `\nm{count of pairing cells drawn in the schematic}` both move.

4. **The manuscript is now 12 pages, up from 11.** The growth is entirely required additions: the
   Methods "Container census" subsection (had no home), the two-oracle disambiguation paragraph, and
   longer self-contained captions. If the venue budget is hard, the cheapest recoveries are Methods
   "Correctness oracle and differential testing" (currently the longest subsection that carries no
   claim) and Table 1's caption. Flagging rather than cutting, because both are content decisions.

Additionally flagged, no action taken:

- **`references.bib` untouched**, per instruction. 32 citations remain unresolved; the checker's
  reference-order requirement cannot be fully verified until the bibliography builds.
- **6.11**: Table 2's caption declares a bold/plurality convention that no cell yet uses.
- **Concurrent-edit hazard**: three files were modified underneath this audit by another agent. A
  re-run of `check_display_items.py` and one `latexmk` pass should gate any further parallel work in
  `sections/`.
