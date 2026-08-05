# Pitch kit — "Even Faster Positional-Population Counts"

The manuscript stays Nature-calibrated. This file is where the volume goes to 11:
arXiv listing, cover letter, announcement thread, talk title, lab-blog framing.
Every claim below is backed by a number in the paper — nothing here oversells
past the evidence; it just chooses the most electric true sentence available.

---

## The one-liner

> A GPU program that evaluates 100 million circuits per second — and knows the
> silicon's port map and latencies — just unseated a bit-counting circuit that
> humans have copied unchanged since a 1997 Usenet post.

Variants by audience:

- **Systems crowd:** "We made the state-of-the-art pospopcnt kernel look like
  the baseline: ~2× on NEON across every cache tier of three microarchitectures,
  including the exact core the prior paper tuned on — with 40% fewer retired
  instructions."
- **PL/synthesis crowd:** "First exhaustive, ISA-cost-aware search over
  in-register compressor-tree topologies. SAT proves the floor, a Metal engine
  covers hundreds of billions of candidates, and die-specific timings (port
  contention measured on Graviton 4, Firestorm latency tables) decide the
  ranking. Equal-instruction-count circuits rank differently — and the hardware
  agrees with the model."
- **AlphaTensor-adjacent framing:** "Machine-discovered arithmetic circuits
  beat 28 years of human designs — for population counting instead of matrix
  multiplication, with exhaustive verification instead of RL."

## Why this is cool (the three hooks, in selling order)

1. **The 28-year blind spot (and the 50-year lag).** Every popcount and
   pospopcnt kernel since 1997 — Warren's *Hacker's Delight*, Muła–Kurz–Lemire,
   Klarqvist et al., Clausecker et al. — uses the same 3:2 carry-save tree.
   Every advance in the lineage moved along other axes (vectorization, wider
   registers, schedules); the circuit itself never moved once. Hardware
   answered this exact question half a century ago — higher-order compressors
   have been standard in silicon multipliers since the 1970s — and software
   never imported the answer. The last paper in the lineage even wrote down
   the conjecture ("there is likely a different optimal design for each ISA")
   and declined to test it. We tested it. It's true. The canonical circuit
   loses. First forward motion on the topology axis in the lineage's history.
2. **The search knew the silicon.** Correctness alone doesn't pick a winner:
   the objective ingests *die-specific* timings — eor3/bcax sharing a single
   vector pipe on Neoverse V2 (measured by our own calibration harness under
   hardware cycle counters), Firestorm's 2-cycle latency / 4-wide issue on
   Apple. Two circuits with identical instruction counts rank differently, and
   the silicon confirms the ranking: the balanced-roots tree wins on Apple
   exactly as the model predicts.
3. **Code optimized to optimize code.** The searcher is itself a
   performance-engineered artifact: one candidate circuit per GPU thread, the
   full 128-row truth table packed into four 32-bit words, ~10⁸ candidates/s
   sustained, 78 billion circuits in one 15-minute run, >2.3×10¹¹ in the
   retained logs. Exhaustive correctness, not sampling. SAT closes the floor.

## The fourth hook: we publish the graveyard

Most optimization papers show you the winner and let you believe it was the
first guess. This release ships the complete variant record — 100+ kernels,
including every design that failed the correctness oracle or lost its
benchmark — each labeled with why. The search is auditable end to end, and the
dead ends are documented so nobody has to rediscover them. Reviewers can't ask
"what did you try?" — it's all there, timestamped, with logs.

## Numbers that stop a scroll

- ~2× peak throughput over the published state of the art on **every** core
  tested (1.98× Graviton 3, 1.85× Graviton 4, 1.80×+ Apple M2 Pro).
- Faster in **48 of 48** tier means: 3 cores × 4 word widths × 4 cache tiers.
- 38.8–41.7% fewer retired instructions per word — the load-invariant signal.
- Wins on the exact silicon (Neoverse V1) the prior kernel was tuned on — and
  wins *biggest* there.
- 25–38% fewer instructions on AVX-512 even where DRAM caps throughput.
- >2.3×10¹¹ candidate circuits exhaustively verified in retained logs;
  campaign ≈10¹². No four-instruction 5:3 compressor exists (SAT-proved);
  ours has five.
- Zero calibration, zero dispatch, zero end-user benchmarking.

## Title options (paper / talk / blog)

1. (current) *Even Faster Positional-Population Counts: Exhaustive,
   ISA-Cost-Aware Search over Compressor-Tree Topologies*
2. *The Circuit Nobody Questioned: Machine Search Retires the Harley–Seal
   Tree on ARM* — talk title
3. *A Trillion Circuits Later: Why the Canonical Popcount Tree Was Never
   Optimal* — blog title
4. *Teaching a GPU the Port Map: ISA-Aware Circuit Discovery for Bit-Parallel
   Kernels* — methods-venue framing

## Cover-letter paragraph (venue submission)

> This manuscript reports, to our knowledge, the first change of reduction-tree
> topology in the 28-year Harley–Seal population-count lineage, and the first
> demonstration that a machine-discovered 5:3 compressor tree outperforms the
> hand-designed 3:2 tree that every prior implementation employs. The result is
> established by an exhaustive GPU search — hundreds of billions of candidate
> circuits verified against full truth tables — ranked under execution-port and
> latency models measured on the target silicon, a coupling absent from the
> exact-synthesis, superoptimization, and hardware compressor-tree literatures.
> The practical consequence is a calibration-free kernel family that outperforms
> the published state of the art at every cache tier and word width on Neoverse
> V1, Neoverse V2, and Apple M-series — including the core on which the prior
> work was tuned — while retiring ~40% fewer instructions per word. The search
> engine, kernels, proofs, and raw measurements are released in full.

## Thread skeleton (announcement)

1. In 1997 someone posted a bit-counting trick to a newsgroup. It's been in
   every SIMD popcount kernel since — Hacker's Delight, AVX2, AVX-512, NEON.
   Nobody ever asked if the circuit itself was optimal. We asked. 🧵
2. We built a Metal engine that evaluates ~100M candidate circuits/sec — each
   one checked against its FULL truth table, no sampling. 231 billion
   candidates in the logs. SAT proves the lower bounds.
3. The twist: correctness doesn't pick winners. The objective ingests
   die-specific timings — measured port contention on Graviton, Firestorm
   latency tables on Apple. The search sees what the pipeline sees.
4. Result: the canonical 3:2 tree loses. A 5:3 compressor family (provably
   instruction-minimal) beats it — ~2× throughput over the published SOTA on
   every core we tested, 40% fewer instructions retired.
5. Including on the exact CPU the previous paper tuned on. Especially there.
6. Everything is open: engine, kernels, proofs, raw logs. If your kernel has a
   small bit-parallel circuit inside, this search works on it too. [link]

## Evidence roadmap (what would make each beat land harder)

Ranked by impact-per-effort. Items marked [ME] the assistant can gather;
[HW] need benchmark hardware time.

1. **Dependency census** [DONE — receipts below] — converts "kernel faster"
   into "the circuit inside things you use was wrong."
   - **CRoaring** (1.9k★): explicit `avx2_harley_seal_popcount256` /
     `avx512_harley_seal_popcount512` in `bitset_util.h`. CRoaring is the C
     engine for Roaring bitmaps; its documented adopters include ClickHouse,
     Apache Doris, StarRocks, Redpanda. (Keep Java-side Roaring users —
     Lucene, Elasticsearch, Druid, Spark — OUT of the CSA-specific claim:
     the Java implementation has its own popcount.)
   - **libpopcnt** (369★): AVX2 kernel is the Lemire–Kurz–Muła Harley–Seal
     CSA (comment cites the paper). Vendored by libcimbar (6.2k★) and
     tilemaker (1.9k★), packaged in vcpkg; 174 GitHub code hits for the
     header. Scope claim to AVX2 — its AVX512/NEON paths use native popcount.
   - **primesieve** (1.1k★): carries its own Harley–Seal port in
     `src/popcount.cpp` (cite that file, not a libpopcnt dependency).
   - **pospop** (Go, Clausecker): CSA-based, zero public importers — don't
     claim adoption for it.
   - Sentence for PR: "The circuit this paper supersedes ships today inside
     the bitmap engines of ClickHouse-class databases and in bulk-popcount
     libraries vendored across thousands of projects."
2. **Out-of-sample prediction** [HW, highest scientific value] — build the
   cost model for a core the search never saw (M3/M4 or Neoverse N2), have it
   PREDICT the winning leaf, then benchmark. If it calls the winner, the
   method graduates from optimization to instrument. One run.
3. **Search-cost accounting** [HW, trivial] — sum logged GPU wall-clock ×
   measured M2 GPU power (powermetrics). Target stat: "the search that
   overturned 28 years of designs used ~$1 of electricity."
4. **Energy per terabyte** [HW] — powermetrics (Apple) / RAPL (Sapphire),
   both kernels: joules/TB. The beat that survives the memory wall, and the
   honest cash-out of "40% fewer instructions."
5. **Browsable graveyard** [ME] — static page over the 151-variant record
   (name, status, cause of death). Screenshotable transparency.

## Framing discipline (what we never say)

- Not "AI discovers" — it's exhaustive search + SAT + measured cost models.
  That's a *strength*: deterministic, reproducible, auditable.
- The 5:3 novelty is always scoped "in software / in-register": 4:2 and 5:3
  compressors are 50-year-old *hardware* ideas (Wallace/Dadda multipliers).
  Our claim — verified adversarially against Muła's archive, Aptroot, Knuth,
  and all citing literature — is that nobody ever brought them into the ISA,
  and that they win there.
- Not "optimal kernel" — the *compressor* is proved minimal; kernel-level
  claims stay empirical, per-core, per-tier.
- The trillion is stated as a derivation, not a bare count: 2.3×10^11 in
  retained logs (auditable floor) + ~100 multi-hour runs × measured rates
  → >10^12 campaign total. Lead with the floor, then the arithmetic.
- The rival kernel is always "Clausecker NEON" — correct name, full cite.
  The scoreboard does the talking.
