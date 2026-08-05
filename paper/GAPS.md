# GAPS.md — Adversarial gap analysis for the paper as specified in NARRATIVE.md

Written against `paper/NARRATIVE.md` (orchestrator narrative, 2026-08-05 state), `PROBLEM_STATEMENT.md`,
`RESEARCH_PLAN.md` §7/8/11/14/15/16/20–24, `AGENTS.md`, `LANDSCAPE.md` §4/7/9, and `bench/`+`kernels/` as
they exist on disk. `results/` has been deleted and is being regenerated — nothing below depends on a
number in `results/`; it depends on what the *code* can and cannot currently produce.

Role: a hostile-but-fair reviewer for *Software: Practice and Experience* who wants to reject this paper,
followed by constructive redirection for the campaign that is about to run.

---

## 1. The rejection letter

### Criticism 1 — There is no system here, only a map of one, and the paper's own thesis says composition is the contribution

The thesis (NARRATIVE §2) is explicit: *"the composition is the contribution, not any component."* But
`RESEARCH_PLAN.md` §16.3 says, in the same repository, at the same date: *"Every result from C11 onward
lives in `bench/`. `storm.h` still exposes the 2019 API. The pairing matrix, the gate, the tile pipeline
and the thresholded mode are none of them reachable through the C ABI. There is at present no way for
anyone outside this repository to use any of it."* NARRATIVE §8 confirms this and elects "publish as an
empirical study" rather than closing the gap.

A reviewer who has read Roaring's own papers — the explicit model this paper asks to be judged against —
will notice the asymmetry immediately: Roaring's contribution *was* the packaged composition, benchmarked
end to end, with the container-selection cost included in every number because it could not be excluded
(it is inside `roaring_bitmap_and_cardinality`). This paper's strongest numbers (A3/A4, Table 1) are
**best-cell-per-corpus, not the shipped selector** (NARRATIVE L7: "the paper must never present an oracle
number and a selection-cost number as if they compose into an end-to-end result"), and the tile pipeline
that *would* compose them measures **only within-tile, block-diagonal pairs** with the transpose-build cost
excluded by an amortisation argument (L8). Put plainly: the paper claims to have measured a system and has
in fact measured a matrix of numbers that have never been run together, once, on one corpus, end to end.
For an SPE "implementation of an optimized library" style paper, that is close to disqualifying.

**What would defuse it.** One end-to-end number, per real corpus: `storm_allpairs()` (or the current
in-repo equivalent — `bench_allpairs`/`bench_tile` composed, not run separately) under `Policy::Probe`,
against `run_optimize()`-tuned CRoaring, with selection cost, zone-map/rank-index build cost, and — if the
transpose/tile machinery is used — the transpose build charged, not amortised away. It does not need the
full C ABI (§16.4–16.7 is weeks); it needs one benchmark binary that does not separate "the best cell" from
"the cost of finding it" on the same corpus in the same run. `bench_allpairs.cpp` and `bench_baseline.cpp`
already contain everything needed except the corpus loader in one and the CRoaring call in the other —
gluing them is a day, not weeks.

### Criticism 2 — The headline "17 of 17, beats tuned CRoaring" ratios are precision claims the harness itself says are not trustworthy

NARRATIVE L6, in the paper's own limitation ledger: *"repeats span 16.1–18.2× on one corpus and 1.2–3.7×
on another, the winning cell itself changes on 5 of 17 between repeats, and the file documents an
uncorrected pessimistic bias from thermal and cache-pollution accumulation across the back-to-back
batch."* Table 1 — described in NARRATIVE §6 as *"the paper: the winning-cell column is the result, more
than any ratio in it"* — is built from exactly this harness. A reviewer does not need to find this
themselves; the paper's own supplementary material hands it to them. Worse, the winning cell changing on
**5 of 17 rows (29%)** between repeats is not a precision footnote, it is a threat to claim A4 ("the
winning cell migrates across corpora") — if the migration itself is partly measurement noise rather than
a property of the corpora, the paper's second-strongest claim is compromised, not just its first.

Compounding this: C11 — "Roaring's container threshold is tested on the wrong statistic," the mechanism
NARRATIVE §3 calls *"the sharpest single piece of evidence we have"* and stages first in the framing — is
explicitly flagged in the same document as **"requiring independent verification before the paper leans
on it"** and, as far as the repository shows, has not yet received that verification against CRoaring
`master` (only against the pinned `v4.7.2` vendored copy).

**What would defuse it.** A corrected-harness rerun (interleaved repeats across corpora — the pattern
`bench/filter_ablation.sh` already uses successfully with 9 interleaved repeats — rather than back-to-back
per-corpus batches), reporting a median and a range per corpus instead of a point ratio, with the two
already-flagged unquotable corpora (`uscensus2000`, `dimension_033`) either fixed or genuinely dropped. And
a short, cheap, separate verification of C11 against unpinned CRoaring `master` before it is allowed to
carry the introduction.

### Criticism 3 — Genomics dominates the evidence base of a paper whose own scope rules forbid it from being a genomics paper

`PROBLEM_STATEMENT.md` §8 (non-goals): *"No genomics tooling... Storm stays domain-agnostic: no genotype
encodings, no VCF/`.pgen` awareness, no r²/D′."* `AGENTS.md` rule 1: *"Scope is kernels and algorithms. No
application layer."* Yet `RESEARCH_PLAN.md` §17–19 and §22–23 — five full sections, several hundred lines —
are a genomics case study: 1KGP3, msprime coalescent simulation, gnomAD allele-frequency spectra, UShER
SARS-CoV-2, four *successive corrected claims* about what genomic data does and does not demonstrate
(C8 → C25 → §19.4 → C31 → C32). NARRATIVE's own reconciliation #4 concedes the problem exists and tries to
pre-empt it ("genomics is not the motivating application... say that; do not say the method was built for
genomics"), but a reviewer reads Results and Methods, not intent statements. If R4/R5/R6 spend a
disproportionate share of their word budget on population-genetics bimodality, allele-frequency spectra and
haplotype-major vs. variant-major layouts, the paper reads as *Tomahawk's motivation section wearing a
kernel paper's title* — precisely the self-overlap `RESEARCH_PLAN.md` §8b says publishing Storm first was
supposed to avoid, and precisely what the "never concurrently" sequencing rule in `PROBLEM_STATEMENT.md` §9
exists to prevent.

**What would defuse it.** A hard word-budget cap on genomic material in Results (one corpus, one paragraph,
folded into Table 1 like every other corpus, not a subsection built around it), with the four-claim
correction history moved to Methods or Discussion as a candour example (which NARRATIVE reconciliation #5
already proposes doing) rather than narrated in Results as if it were itself a finding.

---

## 2. Missing experiments, ranked by (impact on the story) / (cost to run)

Legend: **[existing code]** = runnable today with a binary already in `bench/`, no new C++.
**[new code, small]** = under ~100 lines, mostly copy-adapt of an existing pattern already in the tree.
**[new code, real]** = a genuine, multi-day implementation task.

| # | What to measure | Binary / code | Cost | Unlocks |
|---|---|---|---|---|
| 1 | Run `bench_baseline` (the CRoaring head-to-head) on the 8 corpora already fetched/converted but never compared to CRoaring: `enwiki-categorylinks`, `usher_sarscov2`, `gnomad_chr21_exomes_af1e-3`, `dbpedia-link`, `wikipedia_link_en`, `livejournal-groupmemberships`, plus optionally `msprime_10k/100k/1M` labelled as synthetic | **[existing code]** `bench/bench_baseline.cpp` + `bench/run_corpora.sh` (registry just needs the 6–9 extra rows added) | Hours — data is already staged per `bench/all_corpora.sh`'s registry; `enwiki` needs row-budget subsampling exactly as `all_corpora.sh` already does | A3/A4/Table 1. Widens "17 of 17" to ~22–25 and — critically — pulls the single largest number in the entire project (`enwiki-categorylinks`, 37,890× vs all-bitmap, §21.1) into the CRoaring-comparison headline instead of leaving it stranded in a table nobody outside RESEARCH_PLAN.md sees. Directly defuses "cherry-picked 17" objections |
| 2 | End-to-end selector-vs-CRoaring: run `Policy::Probe` (via `bench_allpairs`/`bench_tile`) and `bench_baseline`'s CRoaring call on the *identical* pair sample per real corpus, report both the oracle-best-cell ratio and the shipped-selector ratio side by side | **[new code, small]** — needs a loader shared between the two binaries (the `load_stormbin` function already in `bench/bench_tile.cpp` can be lifted into `bench_baseline.cpp` almost verbatim) plus one combined report script | 1–2 days | Directly answers Rejection Letter #1/#2. Turns L7 from a disclosed limitation into a closed one. This is the single highest-leverage item in this table |
| 3 | Independent verification of C11 against CRoaring `master` (not the pinned `v4.7.2` vendor copy): rerun `roaring_bitmap_statistics()` after `run_optimize()` on `census1881`, `wikileaks-noquotes`, `weather_sept_85`, `census-income` | **[existing code]** — the exact call already used in §15.3, against a second CRoaring checkout | A few hours (clone, build, rerun) | Discharges the flag NARRATIVE §3 places on its own "sharpest single piece of evidence." Cheap insurance against a reviewer doing this in five minutes and finding it wasn't done |
| 4 | Corrected-precision rerun of the 17(+)-corpus CRoaring comparison with interleaved repeats (the pattern `bench/filter_ablation.sh` already uses: 9 interleaved repeats across corpora, not back-to-back per-corpus batches) and reported as median + range, not a point ratio | **[existing code + protocol change]** `bench/run_corpora.sh`, restructured to interleave the corpus loop the way `filter_ablation.sh` already does | ~1 day of harness restructuring + run time | Closes L6. Table 1 stops being a table the paper's own supplement calls untrustworthy to 2 sig figs |
| 5 | Oracle regret (`bench_regret.cpp`) on real corpora, not just synthetic — the metric §5.1 calls "the metric that makes this a contribution" has never been measured off-generator | **[new code, small]** — port the ~30-line `load_stormbin` loader from `bench_tile.cpp` into `bench_regret.cpp` | Half a day | A6/L1. Currently A6 (measured selection beats modelled selection) and L1 (nothing within 1.5× of oracle) are synthetic-only claims about a paper whose flagship evidence (Table 1) is real-corpus. Closing this makes the regret story and the corpus story the same story |
| 6 | Zone-map bin-width sweep (currently hardcoded at 512 bits, `OCC_BIN_WORDS`, flagged in `RESEARCH_PLAN.md` §12.7 as "only ever measured at one bin width") | **[new code, small]** — parameterise the constant (template arg or runtime field), rerun `bench_cells`/`bench_baseline` at 3–4 widths on 2–3 representative corpora | 1–2 days | Strengthens the zone map's claim to be *the* mechanism (Fig. 1, R1/R2) rather than one that was tuned once and never checked for sensitivity. Cheap relative to how central the mechanism is to the thesis |
| 7 | Cross-ISA rerun of the real-corpus campaign (§15 is Apple M4 only; L5 names this explicitly) on the 3 remote hosts already used for the synthetic cross-ISA sweep (`fpga-neo1`, `fpga-neo2`, `fpga-sapphire`) | **[existing code]**, contingent on host availability — `bench/run_corpora.sh` needs no changes, just execution on the other hosts | Depends entirely on remote-host access/scheduling, not on engineering | Directly fixes L5. R4/R5's entire evidentiary basis is currently one microarchitecture; this is explicitly named in `RESEARCH_PLAN.md` §15.6 as "the top remaining task" |
| 8 | Charge the `build_occ()`/rank-index build cost inside the timed region at least once, rather than building rows before the clock starts (flagged open in §15.8: "the claim is currently 'free to use,' not 'free to build,' and only the former is measured") | **[new code, small]** — a build-cost variant of `bench_baseline`/`bench_cells` that times `build_row()` itself, amortised over N | Half a day | A9/A10. Closes a specific, named, self-identified gap before a reviewer asks "what does it cost to build this metadata in the first place?" — especially pointed given C33 (§24.2) shows that metadata is sometimes 26,000× larger than the data it describes |
| 9 | Cross-tile (true N-row) all-pairs measurement, replacing the block-diagonal-only tile pipeline (L8) | **[new code, real]** — this is materially the ABI-wiring work `RESEARCH_PLAN.md` §16.3–16.7 estimates at "weeks." Requires the transpose/selector machinery to actually iterate a full corpus, not one representative tile | Days to weeks | Closes the biggest structural gap named anywhere in this analysis (Rejection Letter #1). Highest total impact in this table, but the only item that cannot be done inside "the final runs" window described by the task; flag it as the item that determines whether the paper is the empirical study (option a, NARRATIVE §8) or the systems paper (option b) |
| 10 | A same-corpus filtered/unfiltered genomics pair (e.g. gnomAD chr21 with and without the AF<1e-3 cut, both loaded through `bench_allpairs`) — the comparison C31 needed and never got, per §23's own retraction note: "that comparison needed the *same* corpus with and without the AF cut, which was never run" | **[existing code]** — `bench_allpairs --file` already accepts a stormbin corpus; needs the unfiltered gnomAD chr21 export in addition to the AF<1e-3 one already used | A data-prep step (extract the unfiltered chr21 from the same source) + an hour of compute | Either closes or permanently retires the filtering question raised and dropped across C29/C31/C32. Cheap relative to how many times this project has already reasoned about it incorrectly (§22.5 lists four corrected genomic claims) |

Item 9 is the one item on this list that is genuinely "needs new code" at real cost — everything else is a
rerun, a small loader port, or a parameter sweep on infrastructure that already exists. If the campaign has
to choose, items 1–5 are all cheap and directly answer the three rejection-letter criticisms; do those
first regardless of what else gets prioritized.

---

## 3. The baseline question

**Current state.** `RESEARCH_PLAN.md` §7.3 specifies six baselines: scalar `popcount` loop, `libpopcnt` +
manual AND, CRoaring in an O(N²) loop, SimSIMD Jaccard kernels, the published Muła/Kurz/Lemire Table 4
figures, and Storm-2019. `RESEARCH_PLAN.md` §11 ("Definition of Done — target") requires beating "every
baseline in §7.3." **The code contains exactly one of these**: `bench/bench_baseline.cpp` links CRoaring
(`third_party/croaring_amalg`) and nothing else — no `libpopcnt`, no SimSIMD/NumKong, no hash-set
comparison, no BitMagic, no GraphBLAS. `grep` across `bench/` and `kernels/` for any of these turns up
nothing. The plan promised six baselines; the campaign has one, plus the internal all-bitmap comparator.
This is a real, checkable gap between what `RESEARCH_PLAN.md` set as the bar for "done" and what the
current code can produce, and NARRATIVE's own §6 claim ledger silently narrows to CRoaring + all-bitmap
without flagging that this is a reduction from §7.3/§11's original spec. That silent narrowing is itself
worth a line in the paper ("we compare against CRoaring rather than the broader baseline set in [internal
plan] because X") rather than leaving a reviewer to notice the plan promised more.

**Is CRoaring + all-bitmap sufficient for SPE?** Marginally, if and only if the paper is honest that
CRoaring is a *very* strong incumbent baseline (which it is, and NARRATIVE §3 says this correctly — "not
'Roaring is slow'") and the all-bitmap column is explicitly labelled a straw man (which L2/reconciliation
#3 already do). A reviewer steeped in this literature (a realistic profile for SPE, given the venue is
Roaring's own) will still ask for at least one of the below by name:

- **Hash-set intersection** (e.g. `std::unordered_set`/`robin_hood::unordered_set` AND, or a Bloom-gated
  hash probe). **Worth adding — cheap and directly relevant.** `bench_bloom.cpp` already builds hashed
  bucketing machinery for the zone-map comparison; the marginal cost of a plain `std::unordered_set`
  intersection-cardinality baseline on the same real corpora is low (a few hours), and it closes an
  obvious objection: "why not just hash the sparse side?" is the first question anyone from a
  database/algorithms background will ask about a sparse-set intersection paper, and the current evidence
  has no answer to it at all.
- **Plain sorted-array merge, no adaptivity** (e.g. `std::set_intersection`-style, or a vanilla two-pointer
  merge with no SIMD, no galloping, no representation switching). **Worth adding, near-free.** The paper
  already has S×S kernels; reporting the *dumbest possible* correct sorted-merge alongside them as an
  explicit floor (parallel to the existing "scalar `popcount` loop" floor for B×B) costs essentially
  nothing and gives the reader an honest low end for the S×S column the way `RESEARCH_PLAN.md` §7.3
  intended the scalar-popcount floor to work for B×B.
- **BitMagic** (`bm::aggregator<>`). **Not worth the effort for this paper.** `LANDSCAPE.md` §5 already
  characterizes it correctly: fan-in aggregation ("AND many into one"), compile-time-only SIMD dispatch,
  not an all-pairs primitive. Benchmarking it would require writing an all-pairs driver around a library
  not designed for the task, producing a comparison a BitMagic maintainer could fairly call unrepresentative
  of their tool's intended use. Cite it in Software Landscape (§5 of `LANDSCAPE.md` material); do not
  benchmark it.
- **Lucene's `SparseFixedBitSet` approach.** **Not worth building.** It is Java, embedded in a search
  engine, and its "coarser summary of non-empty blocks" idea is already the zone map — it is correctly cited
  as prior art for the *mechanism* (`PROBLEM_STATEMENT.md` §2.6, `LANDSCAPE.md` §9.1) rather than as
  something to reimplement and race. Reimplementing it in C++ just to benchmark it would be weeks of
  engineering to produce a number that restates a citation.
- **GraphBLAS (SuiteSparse) masked `GrB_mxm`.** **Genuinely worth considering, and currently absent.**
  `RESEARCH_PLAN.md` §12.4 identifies it as *"the closest existing system"* — four runtime formats, format
  and algorithm selection per operand — and states *"our claim must be stated against GraphBLAS explicitly
  rather than against naive all-bitmap."* That sentence is a plan-level commitment the current evidence does
  not honor: nothing benchmarks against GraphBLAS. This is more expensive than the hash-set or sorted-merge
  additions (GraphBLAS's boolean semiring all-pairs formulation would need a driver, and its per-tile format
  selection is not the same operation as cardinality-only intersection, so a fair comparison takes real
  design work) but it is the one omission on this list that the project's own planning document already
  flagged as necessary and that a reviewer familiar with sparse linear algebra will notice by name.
- **An all-pairs-specific system** (e.g. a similarity-join engine such as Bayardo/Ma/Srikant-style APSS, or
  chemfp/OpenBabel's N×N mode). **Not worth building new code for.** These prune pairs below a threshold —
  a different problem (`PROBLEM_STATEMENT.md` §8 explicitly rules pruning out of scope) — and the paper
  already handles this correctly by citing them as a different objective (`LANDSCAPE.md` §4.4) rather than
  as competitors to beat.

**Recommendation, in priority order:** (1) add the hash-set and plain-sorted-merge floors — cheap, directly
answer the two questions a systems reviewer asks first, and reuse infrastructure that already exists; (2)
either build a GraphBLAS comparison or explicitly and visibly walk back the RESEARCH_PLAN §12.4 commitment
to compare against it, rather than silently dropping it; (3) do not spend engineering time on BitMagic,
Lucene's SparseFixedBitSet, or a bespoke APSS system — cite, don't race.

---

## 4. The narrative holes

**Claims without a display item.** NARRATIVE §6's abstract-clause audit checks four clauses ("wins zero of
25," "17 of 17," "0.03–0.37% selection cost," "U-shaped") against Figs. 2–4 and Table 1. It is silent on
**C11** — the claim NARRATIVE §3 itself calls *"the sharpest single piece of evidence we have"* and stages
first in the two-question framing. Checking the display-item table (§6) directly: the "Serves" column reads
`thesis, R1` / `A2` / `A1, A8` / `A5, A6` / `A3, A4` / `A10, L3` across Figs. 1–4 and Tables 1–2. **C11
appears in no cell of that column.** R5 — the Results subsection built entirely around C11 (the container
census showing `census1881` has zero bitset containers at 1.2×10⁻³ density) — has no dedicated figure or
table anywhere in the six main display items. Its evidence (the per-corpus array/bitset/run container
counts, `RESEARCH_PLAN.md` §15.3) currently lives only in prose and would-be supplementary material. This
is precisely the failure mode item 4 of the task asks for: a Results subsection whose heading promises more
than its evidence delivers, on the paper's most-hyped single mechanism.

**A1 rides on a figure built for a different measurement.** Fig. 3 is specified as "(a) SIMD advantage vs.
working-set size... (b) work-avoidance advantage... over the same axis" and is tagged as serving **both**
A1 ("SIMD wins 0 of 25 asymmetric cells," a discrete count from a 5-cell × 5-corpus-shape grid) and A8 ("SIMD
advantage decays with working-set size," a continuous curve from a residency sweep). These are two different
experiments with two different independent variables (cell identity vs. working-set size). A working-set
decay curve does not visually communicate a "0 of 25" count; at best it is suggestive if the curve dips
below 1.0×, but that is not the same claim. The paper's single most quotable sentence — SIMD wins *zero* of
25 — currently has no figure that shows the number 0 or the 25 measurement points directly. This wants
either its own small panel (a 5×5 grid, cell vs. corpus shape, colored by win/loss) or an explicit
acknowledgment that it is a table-only claim, not a figure claim.

**R6 (failure modes) is thin relative to its billing.** Of L1/L2/L3, only L3 (the metadata defect) has a
dedicated display item (Table 2). L1 ("nothing within 1.5× of oracle") is only indirectly inferable from
Fig. 4's regret bars. L2 (the 0.93× haplotype-major result, described in NARRATIVE §9 reconciliation #1 as
the low end of the entire honest range: "0.93× (haplotype-major chr20, where all-bitmap correctly wins) to
four figures on sparse graph corpora") has **no display item at all** — a number this load-bearing (it
anchors the low end of the paper's central range claim) is currently asserted only in prose.

**The authority chain contains a document that is often wrong.** `AGENTS.md` and `CLAUDE.md` both state
`PROBLEM_STATEMENT.md` has "highest authority — wins all conflicts." NARRATIVE's own preamble concedes this
document "describe[s] the project as it was framed before Phase 2 measured" and that later
`RESEARCH_PLAN.md` sections routinely correct it. Concretely: `PROBLEM_STATEMENT.md` §7 still lists P3
("vectorized asymmetric kernels beat scalar... over a measurable density band") as an open falsifiable
claim; §7.1's evidence-status table in the same file marks it **REFUTED**. The 10⁴×/10⁵× haplotype figures
are stated as the motivating case in §2.1 of the same "highest authority" document and are explicitly
excluded from the paper by NARRATIVE §2 ("What the paper must not claim... the original 10⁴×/10⁵× haplotype
figures, never measured at that scale"). This is not a paper-facing problem — a reader of the paper will
never see `PROBLEM_STATEMENT.md` — but it is a process risk for the drafters: an "authority wins all
conflicts" rule that points at a stale document will, if followed literally, reintroduce retracted claims.
Worth a one-line note in whatever document actually governs drafting, since NARRATIVE already (correctly)
overrides it in practice but the written authority order does not yet say so.

**Ten cells vs. eleven cells.** NARRATIVE §7b flags this itself and has not resolved it: "the paper's 'ten
cells' language refers to the ten non-Roaring cells of the symmetric matrix; C×B is the eleventh and is a
strategy, not a symmetric cell. Be consistent about which count is used and define it once." This is
self-diagnosed but still open — the thesis sentence (§2) says "ten representation pairings," Fig. 1's caption
plan says "ten cells," and §7b's own code inventory lists eleven implemented cells including C×B. A reviewer
who counts kernels in the supplementary optimization record (~180 variants across cells) and gets a
different number than the abstract will not be charitable about it.

**The scale of C4 (complement representation) does not match its billing.** `RESEARCH_PLAN.md` §14.4 calls
building the complement representation *"the paper's strongest novelty claim."* The measured result (C4,
§14.5 and NARRATIVE B5) is a **~20% win** over "the accidental R×R," explicitly stated as "selectable and
statable, not... fast." A claim introduced as the strongest novelty item and delivered as a 1.2× effect with
an explicit instruction to undersell it is a mismatch a reviewer will notice on its own, independent of
whether the paper describes it modestly (NARRATIVE does try to, correctly demoting it to secondary claim B5
and supplementary material) — the risk is only that drafting prose written from the RESEARCH_PLAN's framing
("strongest novelty claim") leaks into Results before the demotion is enforced.

---

## 5. The strongest possible version — three experiments

If exactly three experiments could be added, these are the ones that change what the paper is allowed to
claim, not just how well it claims it:

**1. Wire the selector end to end and report it as the headline, not the oracle.** (Missing-experiment #2
above.) This converts the paper from "here is a map of what wins where" (an empirical study, defensible but
modest) into "here is a system that beats tuned CRoaring, with its own decision cost paid for, on every real
corpus tested" — the Roaring-shaped claim `RESEARCH_PLAN.md` §16 argues for, achievable without the full
weeks-long C-ABI build-out, using only a combined benchmark binary. This is the single change that would
most directly rebut Rejection Letter criticisms #1 and #2 simultaneously, because it removes the oracle/
selector conflation that both criticisms attack from different angles.

**2. Extend the CRoaring head-to-head to the full real-corpus set, especially `enwiki-categorylinks`.**
(Missing-experiment #1.) Right now the project's single most dramatic number — 37,890× over all-bitmap on
129.7M-bit categorylinks postings, the largest universe and lowest density in the entire corpus set — sits
in a table (`RESEARCH_PLAN.md` §21) that nobody drafting from NARRATIVE would currently cite as a headline,
because it has never been compared to CRoaring, only to all-bitmap. Running it through the existing
CRoaring-comparison binary turns the paper's most impressive fact into part of its most-scrutinized table
instead of an orphaned side result, and it does so on a genuinely different domain (IR posting lists) than
the census/graph/genomics corpora already in Table 1 — which materially strengthens the "domain-general, not
genomics-flavored" defense against Rejection Letter #3.

**3. Independently verified, error-barred Table 1.** (Missing-experiments #3 and #4 combined.) Re-run the
real-corpus comparison with the `filter_ablation.sh`-style interleaved-repeat protocol, reporting medians
and ranges, and separately re-verify C11 against CRoaring `master`. This does not add a new capability the
paper can claim, but it is the one item that converts the paper's own limitation ledger (L6, the C11 flag)
from open items a reviewer will find into closed items the paper can point to. Combined with #1 and #2, the
paper would then be able to claim, with a straight face: *a system, not just a map; evaluated end to end
with selection cost paid; on the broadest and largest real-corpus set in this literature; with per-corpus
estimates that survive a second, independent measurement.* That is a materially stronger paper than the one
NARRATIVE currently specifies, and none of the three items requires new algorithmic work — only harness and
integration engineering on top of kernels that already exist and are already tested.

---

## 6. Scope risks

- **Genomics as de facto co-lead evidence.** Covered in Rejection Letter #3. `PROBLEM_STATEMENT.md` §8 and
  `AGENTS.md` rule 1 are explicit: no application layer, no genotype encodings, no domain integration.
  `RESEARCH_PLAN.md` §17–19, §22–23 are, in aggregate, a small genomics paper's worth of corrected claims
  about human variant data. A reviewer will ask why a "kernels and algorithms" paper devotes this much
  Results/Discussion real estate to population-genetics distribution shape. NARRATIVE's reconciliation
  items #4–#5 show the authors are aware of the risk; the risk is that drafting from `RESEARCH_PLAN.md`'s
  much longer genomics narrative inflates it back past what NARRATIVE's discipline intends.
- **The C-ABI / bindings roadmap (`RESEARCH_PLAN.md` §16.4–16.5, Python/Rust/R packaging) does not belong
  in this paper at all**, and NARRATIVE correctly excludes it ("avoid the words 'library' and 'we
  release'"). The risk is purely a drafting-discipline one: `RESEARCH_PLAN.md` §16 is titled *"a library
  with a paper, not a paper with a repo"* and describes a bindings-and-packaging programme in real detail.
  If Methods is drafted with half an eye on that document rather than strictly on NARRATIVE §7b's "what the
  code actually is," promissory language about released bindings could leak in. Enforce NARRATIVE's own
  rule explicitly during drafting review.
- **Threading and load balancing are correctly out of scope** (`AGENTS.md` rule 1, `RESEARCH_PLAN.md` §8b:
  "moved to Tomahawk") and nothing in the current bench/kernel code touches them (confirmed: no threading
  in `bench/` or `kernels/`). This is not currently a risk, but it is adjacent to risk: the multi-level
  zone-map idea and BLIS-style cross-pair panel blocking (`RESEARCH_PLAN.md` §13.2) are described in
  enough implementation detail that a drafter could be tempted to promise them as future work in a way that
  reads as "there is a bigger system coming" — fine as one Discussion sentence, not fine as a subsection.
- **Pruning/thresholded mode.** `PROBLEM_STATEMENT.md` §8 rules out approximate methods and pruning bounds
  explicitly, "in scope only after the pairing matrix works," and flags a hook exists in the thresholded
  API. `RESEARCH_PLAN.md` §16.4's proposed C-ABI surface includes `storm_allpairs_above()` (thresholded
  mode) as part of "one system." If Methods describes the ABI surface from §16.4 wholesale (rather than
  only the parts actually evaluated), thresholded/pruned mode would enter the paper through the back door of
  an interface description, in direct tension with the non-goal that pruning is a Tomahawk-side concern.
- **The 20.6× dense-corpora / bimodal-genomics material risks reading as an LD (linkage disequilibrium)
  teaser.** §22.4's own recommended framing — "human variation is the clearest case... choosing per pair is
  worth 5.67× [later corrected to a different number, §23] over choosing once" — is a sentence about
  *why representation choice matters*, but it is phrased in the vocabulary of a population-genetics result.
  A careless paraphrase of it in the Introduction's motivating paragraph would read as pre-announcing
  Tomahawk's finding before Tomahawk exists, which `RESEARCH_PLAN.md` §8b's sequencing rule (Storm, then
  Tomahawk, never concurrently) is specifically designed to prevent.

---

## Summary for the record

Three defensible rejection grounds exist today: no end-to-end system behind the composition claim, a
flagship table whose own supplementary material calls its precision untrustworthy, and a genomics-heavy
evidence base sitting inside a paper whose scope rules forbid an application focus. All three have cheap,
concrete, mostly-existing-code fixes (§2 items 1–5), and the same fixes are what would let the paper claim
materially more than it currently can (§5). The baseline set is one library deep (CRoaring) against a plan
that specified six; a hash-set floor and a plain sorted-merge floor are worth adding for almost no cost, and
the GraphBLAS comparison the project's own literature review calls necessary (`RESEARCH_PLAN.md` §12.4) is
currently just absent, not deferred with a stated reason. The display-item inventory has real holes: C11 —
the paper's self-described sharpest evidence — carries no figure or table, and two of the three named
limitations in R6 have no dedicated display item either. None of this requires new algorithms; all of it is
integration, harness discipline, and corpus breadth on top of kernels and measurement code that already
exist and are already tested.