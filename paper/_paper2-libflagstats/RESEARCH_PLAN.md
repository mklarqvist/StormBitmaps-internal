# Research Plan — pospopcnt / libflagstats revival (read me first)

> **If you are reading this cold (you forgot the conversation):** start at §1, then §2 (status), then §3 (the plan).
> You are Marcus Klarqvist. You wrote the pospopcnt operator (arXiv:1911.02696) and libflagstats. You now run a medical
> company; this is a part-time PR/credibility effort, not a full-time science job. The goal is **two arXiv papers**, in
> order, with the least effort that is still honest and defensible. Everything you need is in this repo plus one sibling
> repo. This file is the map; the other files are the detail.

---

## 1. The big picture (what and why)

You have **two papers**, and they must go out **in this order**:

- **Paper #1 — the fast kernel.** "Positional population counts are accumulation-bound on wide ARM cores: a
  calibration-free SIMD kernel for AArch64 (and AVX-512)." You spent ~2 weeks (July 2026) and **beat the state of the
  art** (Clausecker–Lemire–Schintke, arXiv:2412.16370) on ARM/NEON by **up to ~1.86–1.89× (Graviton4/Neoverse V2, L2)
  and ~1.80× (Apple M2 Pro, u16 cache-resident)**. Repo: `~/Desktop/Engineering/positional-popcount`. Draft:
  `positional-popcount/NEW_PAPER_DRAFT.md`. **This is the more novel, more current result — publish it first.**

- **Paper #2 — the application.** "Efficient Computation of Bitfield Statistics for Sequencing Datasets" (libflagstats):
  a branchless SIMD reimplementation of `samtools flagstat` using pospopcnt. Repo: **this one**. Draft: `main.tex` +
  `supplemental.tex` (written 2019–2020, never published). Strategy brief: `PAPER_CHANGES.md`. Experiments:
  `EXPERIMENTS.md`.

**Why this order.** The single biggest risk to reviving Paper #2 was "the 2019 kernel it uses was superseded in 2024/25."
By publishing Paper #1 first, Paper #2 gets to cite **your own current-best kernel** and plug it in. The u16 kernel
(where your NEON wins are largest) is exactly the 16-bit SAM FLAG width — the two papers lock together.

**The honest core of both papers (never gloss this):**
- Kernel speedup is real and defensible: ~110× vs samtools' scalar flagstat loop (compute-only, in-memory).
- But no shipping file format delivers the FLAG field as a contiguous, fixed-width, SIMD-friendly column, so the
  end-to-end "700–56,000×" numbers are a *conditional upper bound*, >80% disk+decompression. On equal footing the honest
  number is ~2–3.5×. Lead with the kernel number; frame the format gap as the contribution, not a footnote.

---

## 2. Status snapshot (as of 2026-07-14 — update this section as you go)

| Item | State |
|---|---|
| Literature review 2019→2026 | ✅ done (17-agent swarm). Findings in `PAPER_CHANGES.md` §1. |
| Go/no-go on Paper #2 | ✅ GO, conditional. |
| Paper #1 experiments | ✅ ARM done (Graviton4/Neoverse V2, Apple M2). ❌ Neoverse V1 head-to-head PENDING (the #1 gap). ❌ AVX-512 PENDING (scope out of v1). |
| Paper #1 draft | 🟡 story-arc complete, honest, `‹measured›`/`[PENDING]` marked. Not yet LaTeX. |
| Paper #2 text edits | ❌ not started (can start anytime — no data needed). |
| Paper #2 experiments | ❌ not started (needs a modern CPU + one CRAM). See `EXPERIMENTS.md`. |
| Modern CPU access | ❓ need: one Zen4/5 or Sapphire/Granite Rapids box, one Graviton3 (V1) + Graviton4 (V2). AWS spot is fine. |
| Datasets | ❓ old TB readsets likely gone. Plan uses one public 1000G CRAM instead (`EXPERIMENTS.md` §1.3). |

**Where things live:**
- This repo: `main.tex`, `supplemental.tex`, `flagstat.bib`, `PAPER_CHANGES.md` (strategy), `EXPERIMENTS.md`
  (run-book/manifest), this file.
- Sibling repo: `~/Desktop/Engineering/positional-popcount` — kernels (`pospopcnt.c/.h`), `benchmark.cpp`, `bench`,
  `instrumented_benchmark`, `NEW_PAPER_DRAFT.md`, `results.*/` sweep dirs.
- Original code: github.com/mklarqvist/libflagstats (has `bench`, `instrumented_benchmark`, `utility`).
- Memory (Claude): the assistant's memory files also summarize this; but THIS file is the source of truth.

---

## 3. The plan, end-to-end

Six phases. Each has a **goal**, **concrete steps**, **exit criteria**, and **rough effort**. Phases A and B can run in
parallel (B is pure writing). Do not start Phase E (rewrite #2) until Phase C is green.

### Phase A — Ship Paper #1 (the kernel) to arXiv  ·  effort: ~1–2 focused days
**Goal:** get the ARM/NEON pospopcnt paper posted, establishing priority over Clausecker–Lemire–Schintke.

1. **Fix the headline to the measured numbers.** Use ~1.86–1.89× (Graviton4 L2) and 1.80× (Apple M2, u16). Do **not**
   say "up to 100% / 2×" — a reviewer divides your own columns and gets 1.89. Your draft already uses the right numbers;
   grep it for any "2×"/"100%"/"double" and soften to "nearly doubles / up to ~1.9×".
2. **Close the #1 gap — Neoverse V1 (Graviton 3) head-to-head.** This is load-bearing: you must beat them on the core
   they tuned on. Steps:
   - Launch an AWS Graviton3 instance (c7g). Clone the repo, `make`.
   - Run the identical paired 60-size protocol you used on Graviton4 (see `positional-popcount/scripts/` and the
     `results.*graviton4*paper-paired` dirs for the exact invocation to mirror).
   - Capture `paired-summary.tsv` (MeanPairRatio across the grid, ins/byte, IPC).
   - **Decision:** wins on V1 → claim "intrinsic, not a retargeting artifact." Only ties on V1 → narrow the claim
     honestly to "Neoverse V2 + Apple M-series" (still publishable).
3. **Scope AVX-512 OUT of v1.** Draft §4.6/Discussion says this already — post the ARM/Apple paper now; add AVX-512 in a
   v2 if/when you run it. Do not let `[PENDING]` AVX-512 block the post.
4. **Fill the remaining `[PENDING]` slots or delete the claims.** Apple full-size curve is nice-to-have; if not run,
   state cache-resident-peak only and mark streaming as future work.
5. **Port the Markdown draft to LaTeX** (reuse the `wlscirep.cls` template from this repo, or a plain article/TACO
   template). Real citations for: Klarqvist 2019 (your own), Clausecker–Lemire–Schintke 2024/25, CSA networks (Muła
   2016 / Warren). Remove the §0 planning block.
6. **Courtesy heads-up to Lemire** before posting (you beat his group's follow-up; he's your co-author on the original
   and on Paper #2). Optics only, your call.
7. **Post to arXiv** (cs.DS or cs.PF). Announce with: "the pospopcnt operator I introduced is now an active area; here I
   re-take the ARM lead with a calibration-free kernel."

**Exit criteria:** arXiv ID for Paper #1 exists; headline numbers = measured; V1 either won or claim honestly narrowed.

---

### Phase B — Paper #2 text-only edits (do in parallel with A)  ·  effort: ~1 day, no data
**Goal:** make `main.tex` non-embarrassing on its own. All of this needs zero new experiments. Full detail in
`PAPER_CHANGES.md` §4(A). Checklist:

- [ ] Cite Paper #1 (once it has an arXiv ID) as the current kernel; reposition Paper #2's contribution as the
      *flagstat application + the format argument*, not a SOTA primitive.
- [ ] Cite Bonfield CRAM 3.1 (Bioinformatics 2022) + GA4GH "seven myths"; name `CRAM_OPT_REQUIRED_FIELDS(SAM_FLAG)` →
      `CRAM_BF` in "Modification to samtools"; concede CRAM does selective decoding, contest only throughput.
- [ ] Address the Arrow/Parquet/ADAM/oxbow/polars-bio ecosystem; concede FLAG-as-column exists, contest
      fixed-width/SIMD-friendliness + one-time-parse cost.
- [ ] Split the ~110× (kernel, defensible) vs 700–56,000× (format, conditional, >80% disk) regimes everywhere;
      never headline 56,000× (it's a 441 GB-archive/95 MB-FLAG artifact).
- [ ] Refresh Intro deluge stats to 2023–2025 (UKB 500k WGS Nov 2023; All of Us ~414k WGS 2025; $100–200 genome).
      Add one sentence conceding flagstat is not itself a pipeline hotspot.
- [ ] Add a short "Threats to Validity" section (single-thread 1.9 baseline; CRAM 4.0 draft; the 56,000× caveat; fix the
      "Nanopore" vs "T2T-chrX" label inconsistency).
- [ ] **Strip all `\mdrk{...}` TODO notes** — they currently render into the PDF.
- [ ] Add `.bib` entries: Clausecker2024, Bonfield2022 (CRAM 3.1), GA4GH seven-myths, Stonebraker2005, quickBAM2023,
      ADAM, plus updated UKB/AllofUs refs.

**Exit criteria:** `main.tex` compiles clean, no TODO notes, honest framing in place, Paper #1 cited.

---

### Phase C — Paper #2 minimal experiments (green-lights the rewrite)  ·  effort: ~1–2 days
**Goal:** the smallest set of real numbers that makes Paper #2 current and honest. Full run-book in `EXPERIMENTS.md`.
Run in this order:

1. **Setup** (`EXPERIMENTS.md` §1): modern CPU, pin versions (samtools 1.23/1.24), build libflagstats with a
   `LIBFLAGSTATS_KERNEL=new|2019` switch, get one public 1000G CRAM.
2. **Ceilings** (`EXPERIMENTS.md` §2): fio disk BW, decompress/core, memcpy, pure-load. Cite everywhere.
3. **E-C correctness gate:** new kernel output byte-identical to `samtools flagstat`. Must pass before timing.
4. **E-K kernel microbench:** modern-CPU version of Table `results-simulated`; new-vs-2019 kernel Δ; memcpy crossover.
5. **E1 CRAM projection + kernel:** the 4-arm `CRAM_OPT_REQUIRED_FIELDS(SAM_FLAG)` experiment — the "available-today"
   baseline that replaces the emulated LZ4 table.

**Exit criteria:** manifest rows D1, D2, D3, D4, D5, D6, D10, D11 in `EXPERIMENTS.md` §8 are filled. → rewrite unlocked.

---

### Phase D — Paper #2 strong/novel experiments (optional but high value)  ·  effort: ~2–5 days
**Goal:** turn "revision" into "new contribution." Only if you have appetite.

- **E3 scatter-gather engine:** reader → queue (spinlock vs lock-free vs sharded) → N fused decompress+kernel workers →
  reduction. Show the pipeline becomes disk-bound (~5–7 GB/s NVMe) and the kernel is never the wall. Best single figure.
- **E4 bitplane FLAG codec:** bit-transpose FLAG into 16 dense bitplanes → positional popcount becomes 16 plain
  popcounts, planes compress trivially, constant runs count without decompressing. This is the concrete "missing format
  primitive" and could stand as its own short paper.

**Exit criteria:** manifest rows D7/D8/D9 filled as far as appetite allows.

---

### Phase E — Rewrite and post Paper #2  ·  effort: ~1–2 days
**Goal:** integrate Phase B text + Phase C/D data into `main.tex` and post.

1. Replace Table `results-simulated` with E-K; add the roofline panel (§2 ceilings).
2. Replace the emulated column-store discussion with E1 (real CRAM) as the present-day baseline; keep the emulated
   LZ4/Zstd numbers only as an explicit conditional upper bound.
3. Add E2 (columnar comparison), and E3/E4 if run.
4. Update Discussion to the "format is the missing primitive" framing (Stonebraker lineage; GA4GH ask).
5. Rebuild figures; final compile; strip planning notes.
6. Post to arXiv, cross-listed q-bio.GN + cs.DS. Cite Paper #1.

**Exit criteria:** arXiv ID for Paper #2; both papers cross-cite; done.

---

## 4. Decision gates (the few choices that matter)

- **G1 (Phase A):** Does the kernel win on Neoverse V1? Win → strong claim. Tie → narrow to V2/Apple. Either way, post.
- **G2 (Phase C):** After E-K on a modern CPU, does the ~110× kernel ratio still hold? Expected yes (samtools loop
  unchanged). If it shrank a lot, investigate before claiming.
- **G3 (Phase D):** Does the bitplane codec (E4) win on both size and speed? If clearly yes → consider a third short
  paper / lead with it. If marginal → report as an ablation.
- **G4 (scope):** Re-run the TB-scale end-to-end 700–56,000× table? **Default NO** — reframe as conditional upper bound.
  Only reconsider if a reviewer demands it.

## 5. If you're resuming after a long gap — do this
1. Read §1–§2 of this file.
2. Check the Status Snapshot table; whatever is ❌/❓ is your next work.
3. Open `PAPER_CHANGES.md` (why) and `EXPERIMENTS.md` (how) for the phase you're in.
4. For Paper #1, open `~/Desktop/Engineering/positional-popcount/NEW_PAPER_DRAFT.md` and its §7 "Open data gaps".
5. Update the Status Snapshot and the `EXPERIMENTS.md` §8 manifest as you complete items — those two tables are the
   living state.

## 6. One-paragraph reminder of the whole thing
Publish the ARM/NEON pospopcnt kernel paper first (it's done bar one Graviton3 run and honest number-fixing); it beats
the current SOTA by ~1.9× and re-establishes you at the frontier. Then revive the libflagstats paper as the application:
cite the new kernel, be honest that no format yet delivers FLAG as a fast column (CRAM does selective decode but only
~3× and via scalar varints), replace the emulated benchmark with a real-CRAM `required_fields` measurement plus an
optional scatter-gather and bitplane-codec demo, and frame the missing "vectorization-friendly field-projection
encoding" as the contribution. Lead with the honest ~110× compute number, never the 56,000× artifact.
