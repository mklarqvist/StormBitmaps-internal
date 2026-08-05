# Reviving *"Efficient Computation of Bitfield Statistics for Sequencing Datasets"* (libflagstats)

**Purpose:** decision brief + concrete edit list + experimental plan for re-posting the 2019 draft to arXiv.
**Audience:** M.D.R. Klarqvist (lead), W. Muła, D. Lemire.
**Context:** lead author is now a med-company CTO; this is primarily a PR/credibility exercise, not a journal submission.
**Provenance:** produced from a 17-agent literature/verification swarm (Dec 2019 → 2026 window). All load-bearing
claims below were adversarially verified; confidence is noted where it matters.

**Authorship framing (important):** libalgebra, the positional-population-count operator paper (arXiv:1911.02696), and
libflagstats are **one coherent research program by Klarqvist** — cite across them freely as your own line. The 2024/2025
successor *"Faster Positional-Population Counts"* (arXiv:2412.16370) is **NOT** yours — it is Clausecker, Lemire &
Schintke (Lemire is the bridge from the original author team). Frame it as *independent follow-on that improved the
kernel you originated by 53% and extended it to ARM* — that is a credibility asset ("my foundational operator is now an
active research area"), but never imply you co-authored it; a reader verifies arXiv in seconds.

---

## 0. Bottom line: **GO — conditional, scoped to the PR goal.**

Posting the draft *as-is* would hurt you with exactly the audience a PR post targets. Posting it after ~1–2 days of
targeted fixes (all text-only, plus one optional afternoon of re-benchmarking) gives a defensible, still-uncontested
result with a credible co-author lineage. The difference is cheap.

**Do not claim adoption.** The work is **novel but orphaned**: repos frozen since Dec 2019 (libflagstats 15★,
libalgebra 45★, positional-popcount 55★, zero open PRs, never merged into any tool, no public dependents); formal
citations of the pospopcnt paper ≈ 2. The credibility case is *"solid kernel + good systems argument + live research
lineage,"* not *"widely used."*

---

## 1. What happened 2019 → 2026 (has anyone explored this since?)

**1. The primitive was superseded — by a paper with Lemire on it.**
Clausecker, Lemire & Schintke, *"Faster Positional-Population Counts for AVX2, AVX-512, and ASIMD"*
(arXiv:2412.16370; *Concurrency & Computation* 2025, DOI 10.1002/cpe.70435) beats the Klarqvist kernel by **~53%**
(91 GB/s AVX-512), fixes its short-array weakness (fast from ~4 KiB), adds ARM ASIMD. The original pospopcnt paper was
formally published (*Concurrency & Computation* 2021, DOI 10.1002/cpe.6304). Live carrier today:
`github.com/clausecker/pospop` (Go/C, 70★, last push Nov 2025).
→ *Must cite and defer to the successor.* Silently omitting a faster kernel co-authored by your own co-author reads as
self-unaware. It also **helps** the PR angle: the line you started is now an active, peer-reviewed area.

**2. samtools/htslib never touched the flag-counting kernel.** Verified across NEWS.md 1.10→1.24: flagstat gained
JSON/TSV output and category counts; `-@` threads only the *decode* side; the branchy scalar `flagstat_loop` macro is
byte-for-byte unchanged from 1.9. So the ~110× kernel comparison **still reproduces**, and the niche is unfilled.

**3. CRAM became a partial answer to your own thesis — your biggest exposure.** Bonfield's CRAM 3.1 paper
(*Bioinformatics* 2022, PMC8896640) benchmarks `samtools flagstat`: **7m5s on CRAM vs 22m52s on BAM (~3.2×)** because
CRAM "only decompresses the data series required." htslib ships the projection primitive:
`CRAM_OPT_REQUIRED_FIELDS(SAM_FLAG)` decodes only the `CRAM_BF` series (confirmed in `cram_decode.c`:
`if (fd->required_fields & SAM_FLAG) s->data_series |= CRAM_BF;`). **Bonfield is thanked in your acknowledgements** — a
reviewer will notice if you ignore this. Concede CRAM does selective decoding; contest only its throughput.

**4. A real columnar-genomics ecosystem shipped.** ADAM/Parquet (projected schema that "only materializes the read
flags," flagstat "orders of magnitude" faster than samtools), oxbow (BAM→Arrow), biobear/exon, **polars-bio**
(BAM→Arrow, 4.5 s / 19.3M rows, projection pushdown, Feb 2026). Direct counterexample to "no format does FLAG column
projection" — must be addressed head-on.

**What did NOT happen:** no *shipping alignment format* exposes FLAG as a **contiguous, fixed-width, SIMD-decodable
column**. CRAM projects it as scalar-decoded ITF8 varints in ~10k-record slices; Parquet gives a real column but via
dictionary/RLE page decode (scalar) and only after a one-time full BAM parse nobody runs at population scale; CRAM
remains the SRA/ENA/1000G archive standard. **The central tension survives 2026 — in a narrower, more precise form.**

**Background motivation strengthened:** UK Biobank all ~500k WGS (Nov 2023); All of Us ~414k WGS (2025); $100–200
genome (Ultima UG100, NovaSeq X 25B); ~20k genomes/instrument/year; ~40 EB by 2025.

---

## 2. The honest column-projection framing (do not gloss)

The draft is already half-honest ("efficiently enough," ">80% of time retrieving and decompressing"). Narrow the claim
to what is still true and make it the contribution:

- **Concede:** CRAM does per-data-series selective decoding; `CRAM_OPT_REQUIRED_FIELDS(SAM_FLAG)` exposes FLAG-only
  decode today; it is ~3× faster than BAM for flagstat; Arrow/Parquet tooling materializes FLAG as a projectable
  column now.
- **Contest only throughput:** none deliver the *contiguous fixed-width 16-bit column* a ~10 GB/s SIMD kernel needs.
  CRAM emits variable-length ITF8, scalar-decoded per record, scattered across the archive; even the hand-optimized
  htslib-mod path reaches only 1–6 MB/s — 2–4 orders of magnitude short. The kernel is an **existence proof** of how
  fast the scan becomes once the format stops being the wall.
- **Lineage (makes it precedented, not spin):** Stonebraker & Çetintemel, *"One Size Fits All Is Gone"* (2005);
  C-Store/MonetDB; Dremel→Parquet/ORC; late materialization. Genomics already went columnar for **variants**
  (TileDB-VCF, vcf-zarr); the missing primitive is a vectorization-friendly **field-projection encoding for alignment
  QC scans.** Frame it as a concrete ask to GA4GH/hts-specs.

**Number honesty — separate the two regimes everywhere:**
- **~110×** — kernel-only, in-memory, attributable to pospopcnt. **Defensible; lead with it.**
- **700–56,000×** — libflagstats-on-hypothetical-column-store vs samtools-on-BAM; >80% disk+decompression;
  format-attributable. **Conditional upper bound.**
- **~2–3.5×** — the honest apples-to-apples number: your own Supp. Tables 8–10, both methods reading the same LZ4
  blocks, decompression-dominated.
- The **56,000×** figure is an artifact of a 441 GB archive holding 95 MB of FLAG — report as an extreme illustration,
  never headline it.

---

## 3. Improved motivation (drop "flagstat is a hotspot" — it isn't)

Verified: flagstat is single-pass, dwarfed by alignment/variant calling, usually a byproduct of DRAGEN metrics or
samtools stats/mosdepth. Concede that up front. Then:

- **Scale is real, not projected:** UKB ~500k WGS (2023), All of Us ~414k WGS (2025), $100–200 genome, ~40 EB by 2025.
  Refresh every 2019-era citation.
- **Binding constraint is retrieval, not compute:** single-core gzip/BGZF decode ~30–50 MB/s vs a SIMD kernel's
  ~10 GB/s (corroborated by quickBAM: samtools "benefits little from >10 threads"). Cheap-per-sample QC gets re-run on
  every reprocessing/migration across millions of samples → aggregate cost is large.
- **Reframed question:** not "can the FLAG histogram be computed faster" (trivially yes) but "can the *format* deliver
  the field fast enough to keep such a kernel busy."

---

## 4. Concrete paper edits, in priority order

### (A) MANDATORY — text only, ~1 day, no compute
1. Cite arXiv:2412.16370; reposition the contribution as *the branchless flagstat reformulation + the format
   argument*, not SOTA pospopcnt.
2. Cite Bonfield CRAM 3.1 (2022) + GA4GH "seven myths about CRAM"; name `CRAM_OPT_REQUIRED_FIELDS(SAM_FLAG)` in
   "Modification to samtools" (turns "unexposed subroutines" into a concrete, checkable claim).
3. Address Arrow/Parquet/ADAM — concede it materializes FLAG as a column; contest fixed-width/SIMD-friendliness + the
   one-time-parse cost.
4. Soften abstract/discussion to the narrowed claim; split ~110× vs 700–56,000× regimes explicitly.
5. Refresh deluge stats to 2023–2025 primary sources; add the "flagstat is not itself a hotspot" concession.
6. Add a short **Threats to Validity** section (56,000× is a row-store artifact; single-thread 1.9 baseline; CRAM 4.0
   still draft).
7. **Strip all `\mdrk{Update with new timings}` / `\mdrk{...}` TODO notes** — they currently ship in the PDF. Fix the
   internal "Nanopore" vs "T2T-chrX" label inconsistency (text vs Table).

### (B) STRONGLY RECOMMENDED — ~1 afternoon
Re-run the **self-contained kernel microbenchmark** (`instrumented_benchmark` / `bench --raw-*`; no genomics data — FLAG
is synthesized) on **one modern native-VPOPCNTDQ CPU** (AMD Zen 4/5 or Intel Sapphire/Granite Rapids), state the exact
samtools version. Add a one-paragraph "AVX-512 landscape 2019–2026" note (fused off Alder Lake → broadened via Zen 4/5 →
re-mandated by AVX10). The **Cannon Lake i3-8121U** (shortest-lived x86 SKU, discontinued Oct 2019) is the single
biggest credibility liability. Optional ARM/NEON run for breadth.

### (C) OPTIONAL — probably SKIP for a PR post
Full end-to-end re-run of the 700–56,000× table vs modern multithreaded `samtools flagstat -@`. Expensive; numbers will
*shrink* (htslib decode ~2× faster since 1.9 + threading); never the defensible headline. **Reframe as an explicit
conditional upper bound instead of re-measuring.**

---

## 5. Top risks if (A) is skipped
- Cannon Lake–only + unnamed samtools 1.9 baseline reads as dated.
- Missing the Lemire-coauthored 53%-faster successor looks self-unaware.
- Ignoring CRAM selective decoding / `required_fields` hands a reader a clean "CRAM already does this" kill (worse:
  Bonfield is acknowledged).
- Leading with 56,000× invites "misleading headline."
- Visible TODO notes signal an unfinished manuscript.
- Overclaiming flagstat as a pipeline bottleneck is trivially refuted.
- The Arrow/Parquet ecosystem undercuts "we need a NEW format" if unaddressed.

---

## 6. Proposed NEW experiments

These convert the paper's weakest section (an *emulated* future format) into *measurements on real formats and real
hardware*. Every experiment names the claim it defends and its effort. Correctness gate applies to all: assert
libflagstats output **byte-matches `samtools flagstat`** on every readset before timing anything (cheap; essential for
credibility).

Establish the ceilings first so every result is attributable:
- **Disk read BW:** `fio` sequential read, O_DIRECT, cold cache (e.g. `fio --name=r --rw=read --bs=1M --direct=1
  --size=8G`). Report per-device and per-thread scaling.
- **Decompression BW/core:** standalone LZ4 / Zstd / libdeflate microbench on one block.
- **Memory BW:** `memcpy` / STREAM (you already use memcpy as a reference — keep it).
- **Kernel BW:** in-memory `instrumented_benchmark` (existing).
These four give a **roofline**; the whole story is "which ceiling are we hitting, and does the kernel ever become it?"

### E1 — CRAM column projection + our kernel (the *available-today* baseline)  **[HIGH value / LOW effort]**
Replace the emulated LZ4 blocks with a real file. Pipeline: open CRAM via htslib, set
`hts_set_opt(fp, CRAM_OPT_REQUIRED_FIELDS, SAM_FLAG)` so only the `CRAM_BF` series is decoded, pull FLAGs, feed
libflagstats. Compare four arms on the same CRAM:
1. `samtools flagstat` (stock, full decode) — baseline.
2. htslib + `required_fields=SAM_FLAG` + **scalar** histogram (this is essentially your "htslib-mod").
3. htslib + `required_fields=SAM_FLAG` + **libflagstats kernel**.
4. (reference) in-memory kernel on the extracted FLAG array (upper bound).
**Metrics:** wall-clock, MB/s of FLAG, CPU cycles, and the **decode:kernel time split**. **Expected:** arms 2–3 nearly
identical because ITF8 scalar decode dominates and the kernel is negligible → quantifies "the format is the wall" on a
*standard file*, not an emulation. **Defends:** the honest framing in §2; replaces the paper's most attackable table.
**Note:** confirms/measures that your htslib-mod ≈ `required_fields` (pre-empts "you reinvented required_fields").

### E2 — Benchmark against the modern columnar ecosystem  **[HIGH value / MED effort]**
Materialize one readset's FLAG column in each modern representation and time a full FLAG-projection scan feeding the
kernel (or the tool's native count):
- **Arrow/Parquet** via oxbow (`noodles`→Arrow) or ADAM (`saveAsParquet` with a flags-only projected schema).
- **polars-bio** projection pushdown (`SELECT flag` scan).
- **Genozip** `genocat --FLAG` (a *filter*, not a projection — measure and label as such).
- **Your fixed-width raw column** (existing LZ4/Zstd blocks) as the SIMD-friendly upper bound.
**Metrics:** projection retrieval throughput (MB/s of FLAG delivered to the kernel), one-time conversion cost
(amortization honesty), and whether page/dictionary decode can *saturate* a 10 GB/s kernel (it can't — measure the gap).
**Expected ordering:** BAM ≪ CRAM-projection < Parquet-projection ≪ fixed-width-SIMD-column ≈ kernel-bound. **Defends:**
"columnar already exists" rebuttal — you show *how far* existing columnar is from feeding a SIMD kernel, and that the
missing piece is a fixed-width encoding, not columnar-ness per se.

### E3 — Parallel scatter-gather engine (your proposal, refined)  **[HIGH value / MED-HIGH effort]**
Goal: measure the *achievable ceiling* with a good layout + parallelism, and show where the bottleneck migrates.

**Architecture (refined from your sketch):**
```
                 ┌── reader thread(s) ──┐        bounded MPMC ring          ┌── worker 0 ──┐
 disk / NVMe ───►│ O_DIRECT, io_uring,  │  push  ┌───────────────────────┐  │ decompress + │──┐
                 │ 1–4 MB compressed    │───────►│ [blk][blk][blk]...    │─►│ kernel       │  │  thread-local
                 │ FLAG-column blocks   │  pop   └───────────────────────┘  │ (SoA counts) │  │  flagstat[16][2]
                 └──────────────────────┘         (spinlock OR lock-free)   └──────────────┘  │
                                                          ...               ┌── worker N-1 ─┐  ├─► final reduction
                                                                            │ decompress +  │──┘   (sum → totals)
                                                                            │ kernel        │
                                                                            └───────────────┘
```
Design notes / refinements to your plan:
- **Reader:** decouple I/O from CPU. Prefer **io_uring** (batched async) or a small pool of `pread` threads with large
  (1–4 MB) buffers; use **O_DIRECT** to bypass the page cache so cold-cache numbers are honest (page-cache-warm results
  are the classic reviewer trap). One reader usually saturates NVMe; add readers only if a single one is disk-bound
  below device BW.
- **Queue:** a single spinlock-protected queue **will become the contention point** once workers are cheap — every
  worker hammers one lock per block. Measure it (it's a good baseline and matches your idea), then compare against
  (i) a **lock-free bounded MPMC** ring (Vyukov-style) and (ii) **per-worker sharded queues** with the reader
  round-robining blocks (often the fastest and simplest). Report the spinlock-vs-lockfree-vs-sharded delta — that
  contention story is itself a nice figure.
- **Fuse decompress+kernel in the worker.** LZ4 decompress ≈ 2–4 GB/s/core, Zstd ≈ 1 GB/s/core, kernel ≈ 10 GB/s →
  **decompression is the real CPU cost**, so N ≈ physical cores of fused workers is right; a separate kernel-thread
  pool would just add queue hops. (If you store the fixed-width raw column uncompressed, workers become pure kernel and
  you go straight to the disk/memory ceiling — worth running as the "no-codec" extreme.)
- **Thread-local accumulators**, `alignas(64)` to avoid false sharing; **reduction** is a trivial 16×2 sum at the end
  (negligible). Pin threads (NUMA-aware) and report with/without pinning.
- **Sweep N** = 1 … (physical cores, then ×2 for SMT). Plot throughput vs N and mark which ceiling (disk / decompress /
  kernel / memory) you hit at each point.
**Metrics:** end-to-end GB/s and wall-clock; scaling curve; bottleneck attribution vs the E-ceilings; lock-scheme
comparison; cold vs warm cache. **Expected:** with enough fused workers you become **disk-bound at NVMe BW (~5–7 GB/s)**,
at which point a single-thread 10 GB/s kernel is already more than enough — i.e. *the kernel is never the bottleneck once
the format cooperates.* That is the paper's thesis, demonstrated end-to-end on real hardware instead of emulated.

### E4 — FLAG-specific storage codec, co-designed with the kernel  **[HIGH novelty / MED effort — potential headline]**
FLAG values are extremely low-entropy (a handful of values dominate a whole file). Two layout ideas that a generic
LZ4/Zstd block ignores:
- **Bit-transpose (bitplane / structure-of-arrays):** store the 16 FLAG bits as 16 separate dense bitmaps (1 bit/read
  per plane). Then the **positional** popcount collapses to 16 ordinary `popcnt`/`VPOPCNTDQ` passes over dense
  bitmaps — the "positional" work becomes free, and each plane is trivially compressible (RLE / Roaring / stream-split
  + Zstd) because most planes are almost-all-0 or almost-all-1.
- **Dictionary + RLE / FOR** on the raw 16-bit values (few distinct values, long runs).
**Metrics:** compressed size vs LZ4/Zstd on raw values; decode+count throughput; and crucially whether the transposed
layout lets you **skip decompression entirely for constant runs** (count a run without materializing it).
**Why it matters:** this is the concrete answer to "what encoding is missing" — a layout co-designed with the operator,
which is a stronger and more original contribution than "generic compressor on a raw column." It directly instantiates
the GA4GH/hts-specs ask.

### E5 — Cross-hardware kernel sweep  **[MED value / LOW effort — this is item (B)]**
Re-run the in-memory microbench on: Cannon Lake (continuity) + Zen 4/5 + Sapphire/Granite Rapids (native VPOPCNTDQ) +
Apple Silicon / AWS Graviton (NEON). Include the **Clausecker et al. 2024/2025 kernel** as a drop-in faster core and
report the delta. **Defends:** kills the "dying/rare ISA" objection; shows the ~110× ratio is stable and the successor
kernel plugs in.

### E6 — (optional) Multithreaded fairness baseline  **[LOW value / MED effort]**
If you touch the end-to-end table at all, run stock `samtools flagstat -@ {cores}` on current htslib as the *fair*
baseline so no one accuses you of a single-thread straw man. Otherwise state explicitly in text that the BAM baseline
was single-thread 1.9 and modern multithreaded decode shrinks the ratios.

### Suggested minimal experiment set for a strong-but-cheap PR post
**E5 (B) + E1 + E3.** That trio gives: a modern-hardware kernel number (kills the Cannon Lake problem), an
available-today real-format baseline (kills the "emulation only" problem), and an end-to-end parallel demonstration that
the kernel stops being the bottleneck the moment the layout cooperates (the thesis, shown on real hardware). Add **E4**
if you want a genuinely novel, headline-worthy result rather than just a hygiene refresh — it's the one experiment that
could justify a *new* paper rather than a revision.

---

## 7. Sequencing of work
1. **§4(A) text edits + `.bib` additions** (½–1 day) — makes the draft postable and non-embarrassing on its own.
2. **E5/(B) kernel re-bench on one modern box** (½ day) — biggest credibility win per hour.
3. **E1 CRAM `required_fields` + kernel** (~1 day) — replaces the weakest table with a real-format measurement.
4. **E3 scatter-gather** (~2–4 days) — the end-to-end thesis demo; strongest single figure.
5. **E4 co-designed codec** (~2–5 days) — optional but this is what turns "revision" into "new contribution."
6. Post to arXiv; announce with the "the line I started is now an active peer-reviewed area (carried forward by Lemire)"
   framing.

---

## Appendix — key sources
- Klarqvist, Muła, Lemire. *Efficient Computation of Positional Population Counts Using SIMD Instructions.*
  arXiv:1911.02696; *Concurr. Comput.* 2021, DOI 10.1002/cpe.6304.
- Clausecker, Lemire, Schintke. *Faster Positional-Population Counts for AVX2, AVX-512, and ASIMD.* arXiv:2412.16370;
  *Concurr. Comput.* 2025, DOI 10.1002/cpe.70435. `github.com/clausecker/pospop`.
- Bonfield. *CRAM 3.1: advances in the CRAM file format.* *Bioinformatics* 2022, PMC8896640. (flagstat 7m5s CRAM vs
  22m52s BAM.)
- htslib: `CRAM_OPT_REQUIRED_FIELDS`, `SAM_FLAG=0x2`, `cram_decode.c` (`required_fields & SAM_FLAG → CRAM_BF`);
  NEWS 1.10 "~2× SAM decode, multi-threading"; 1.19/1.21 CRAM & bgzf speedups.
- GA4GH: "Seven myths about CRAM" (selective per-series decode).
- Columnar ecosystem: ADAM/Big Data Genomics (Parquet, flags-only projected schema); oxbow (`noodles`→Arrow); polars-bio
  (BAM→Arrow, projection pushdown, Feb 2026); Genozip (`genocat --FLAG`, *Bioinformatics* 2021).
- quickBAM (*Bioinformatics* 2023, PMC10412403) — flagstat trivial; access is the bottleneck; samtools scales poorly
  past ~10 threads.
- Scale: UK Biobank 500k WGS (Nov 2023); All of Us ~414k WGS (2025); Ultima UG100 / NovaSeq X 25B; ~40 EB by 2025.
- Systems lineage: Stonebraker & Çetintemel, *One Size Fits All Is Gone* (2005); Dremel→Parquet; late materialization.
