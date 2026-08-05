# Experimental Run-Book & Data-Collection Manifest — libflagstats revival

**Scope:** everything to run and every number to capture *before* rewriting `main.tex`. Paired with
`PAPER_CHANGES.md` (strategy/edits). This file is the operational plan: setup → ceilings → experiments → correctness →
a consolidated manifest that maps each captured number to the paper element it fills.

**Two-paper dependency:** the SIMD kernel itself is now Paper #1 (the AArch64/NEON pospopcnt work at
`~/Desktop/Engineering/positional-popcount`). This paper (#2) *cites* Paper #1 and *plugs in* its final u16 kernel — it
does not re-derive kernels. Wherever "new kernel" appears below, it means Paper #1's chosen u16 kernel
(`packed_accum_v124`/epoch248 class, or its final name). u16 is the FLAG width, so the win transfers directly.

**Honesty conventions (mirror the NEW_PAPER_DRAFT convention):**
- `‹measured›` = a number captured under stated conditions in this repo.
- `[PENDING]` = not yet run; do not cite.
- Always report the regime (L1/L2/L3/DRAM or working-set size) and cache state (cold/warm) next to any throughput.
- Separate the **kernel** speedup (compute-only, defensible headline ~110×) from the **format/end-to-end** speedup
  (disk+decompression-bound, conditional). Never merge them in one number.

---

## 0. How to use this file

1. Do §1 (setup) once per machine.
2. Do §2 (ceilings) once per machine — every later result is reported *relative to these*.
3. Run experiments in priority order (§9 gives the minimal set). Each experiment lists: **Objective → Inputs →
   Commands → Capture (table schema) → Pass criteria → Fills**.
4. As numbers land, tick them off in §8 (the manifest). The rewrite starts only when the §9 minimal set is green.

---

## 1. Environment & build

### 1.1 Hardware targets (record exact SKU + `lscpu`/`sysctl` dump per machine)
| Slot | Machine | ISA | Why | Status |
|---|---|---|---|---|
| PRIMARY x86 | AMD Zen 4/5 (Ryzen 7000/9000) **or** Intel Sapphire/Granite Rapids | AVX-512 + native VPOPCNTDQ | replaces the indefensible Cannon Lake i3-8121U | `[PENDING]` |
| ARM | AWS Graviton4 (Neoverse V2) or Apple M-series | NEON/ASIMD | shows the kernel is not x86-only; ties to Paper #1 | `[PENDING]` |
| (optional) legacy | Intel Cannon Lake i3-8121U | AVX-512BW | continuity with the 2019 numbers only | `[optional]` |

Fixed conditions for all runs: pin to one core (`taskset -c N` / `pthread_setaffinity`), disable turbo or record the
sustained clock, `-O3 -march=native`, record compiler (GCC/Clang version), and **cold vs warm cache is stated per run**.

### 1.2 Software pins (record versions — the paper must name them)
```bash
samtools --version | head -1        # target: current 1.23 / 1.24 (paper used 1.9-210 — STALE)
# htslib version is printed by samtools --version
git -C libflagstats rev-parse HEAD  # pin the kernel commit
gcc --version; clang --version
```
Build libflagstats + its bench tools (from the 2019 repo, github.com/mklarqvist/libflagstats):
```bash
git clone https://github.com/mklarqvist/libflagstats && cd libflagstats && make
# yields: bench, instrumented_benchmark, utility
```
Drop in Paper #1's kernel: replace/添加 the NEON u16 path and add a `LIBFLAGSTATS_KERNEL=new|2019` switch so both can be
benchmarked in one binary (needed for the "kernel swap" delta).

### 1.3 Datasets
- **Kernel microbench needs NO genomics data** — FLAG is synthesized `U(0, 2^12)` (Mersenne Twister), as in the 2019
  Methods. This is the cheap, always-runnable path.
- **Format experiments need a real aligned file.** The 2019 TB-scale readsets (NA06994/NA12886/NA12878D/T2T-chrX) are
  likely no longer on hand. Use a **modern, public, reproducible** substitute and state it:
  - 1000 Genomes CRAM, e.g. `HG00096`/`NA12878` from the IGSR FTP (GRCh38, CRAM 3.x). One 30× WGS CRAM (~15–20 GB) is
    enough; downsample with `samtools view -s 0.1` for a fast iteration file.
  - Record: reads, BAM size, CRAM size, raw FLAG bytes, raw MAPQ bytes (rebuild Table `experimental-datasets`).
- Extract the FLAG column (used by E-K in-memory and E4):
  ```bash
  samtools view $FILE | cut -f2 | ./utility > flags.u16   # raw little-endian 16-bit stream
  ```

---

## 2. Ceilings (roofline) — run once per machine, cite everywhere

| Ceiling | Command | Capture | Units |
|---|---|---|---|
| Disk sequential read (cold) | `fio --name=r --rw=read --bs=1M --direct=1 --size=8G --filename=$DEV` | sustained BW, per-thread + N-thread scaling | GB/s |
| Decompression / core | standalone LZ4 / Zstd / libdeflate on one 512 KB block, warm | BW/core per codec & level | GB/s |
| Memory copy | existing `memcpy` reference in `bench` | sustained BW | GB/s |
| Pure-load roofline | read FLAG stream without counting (mirror Paper #1's pure-load diagnostic) | cy/word, GB/s | — |
| Kernel (in-memory) | §E-K below | cy/word, GB/s | — |

**Fills:** a new "Roofline / bottleneck attribution" panel; makes every "we hit the hardware limit" claim quantitative
instead of rhetorical. Expected story: `disk < decompress/core·N < kernel ≈ memcpy` → the kernel is never the wall.

---

## E-K. Kernel microbenchmark (modern HW + kernel swap) — **[MINIMAL SET]**

**Objective:** replace the Cannon Lake-only Table `results-simulated`; show the ~110×-vs-samtools kernel ratio still
holds on a modern native-VPOPCNTDQ CPU, and quantify the delta from swapping in Paper #1's kernel.
**Inputs:** synthesized FLAG, sizes `{1k,10k,100k,1M,5M,10M,100M}` words (as 2019).
**Commands:**
```bash
# per size N, both kernels, all ISAs the box supports:
LIBFLAGSTATS_KERNEL=2019 ./instrumented_benchmark -n $N -i 1000 -v
LIBFLAGSTATS_KERNEL=new  ./instrumented_benchmark -n $N -i 1000 -v
# also the scalar samtools macro path the harness already includes
```
**Capture (fill this table per machine):**

| Input | samtools I/w | samtools C/w | samtools Br.miss | new-kernel I/w | new-kernel C/w | Br.miss | speedup vs samtools | Δ vs 2019 kernel |
|---|---|---|---|---|---|---|---|---|
| 1k … 100M | `[PENDING]` | | | | | | | |

Also capture: throughput (GB/s) vs size, and the memcpy-crossover point (Fig `perf-cannonlake` replacement).
**Pass criteria:** speedup ≥ the 2019 ~45–110× band on ≥1M words; branch-miss count flat (~hundreds) vs samtools' linear
growth; new kernel ≥ 2019 kernel everywhere ≥4 KiB.
**Fills:** Table `results-simulated`, Fig `perf-cannonlake`, the "AVX-512 landscape 2019–2026" note, §Methods versions.

---

## E1. CRAM column projection + kernel (the "available-today" baseline) — **[MINIMAL SET]**

**Objective:** replace the *emulated* LZ4/Zstd column store with a measurement on a **real CRAM file**, using htslib's
own projection primitive. Proves the "format is the wall" claim honestly and pre-empts "you reinvented required_fields."
**Inputs:** one real CRAM (§1.3).
**Build a tiny extractor** `cram_flag_project.c` (htslib):
```c
htsFile *fp = hts_open(path, "rc");
hts_set_opt(fp, CRAM_OPT_REQUIRED_FIELDS, SAM_FLAG);   // decode ONLY the CRAM_BF data series
bam1_t *b = bam_init1(); sam_hdr_t *h = sam_hdr_read(fp);
while (sam_read1(fp, h, b) >= 0) sink(b->core.flag);   // feed scalar hist OR libflagstats kernel
```
**Four arms, same file, cold cache (`echo 3 > /proc/sys/vm/drop_caches`):**
| Arm | What | Capture |
|---|---|---|
| A | stock `samtools flagstat $CRAM` | wall, MB/s FLAG, cy |
| B | required_fields=SAM_FLAG + **scalar** histogram | wall, decode:count split |
| C | required_fields=SAM_FLAG + **new kernel** | wall, decode:kernel split |
| D | in-memory new kernel on `flags.u16` (upper bound) | cy/word, GB/s |

**Capture:** wall-clock, MB/s of FLAG delivered, and the **fraction of time in decode vs count** (perf: `perf stat -e
cycles,instructions` + manual timers around the decode loop vs the count call).
**Pass criteria:** B ≈ C (kernel negligible; ITF8 scalar decode dominates) and both ≪ D → the number that matters is
"how fast can the format deliver FLAG," and it is ~1–6 MB/s-class, orders below the kernel.
**Fills:** replaces the paper's most-attackable table with a real-format result; the honest §2 framing in PAPER_CHANGES;
concretely names `CRAM_OPT_REQUIRED_FIELDS(SAM_FLAG)`/`CRAM_BF` in Methods.

---

## E2. Modern columnar formats — head-to-head projection scan

**Objective:** answer "columnar already exists" by measuring *how far* shipping columnar tooling is from feeding a
~10 GB/s kernel.
**Inputs:** the same readset materialized four ways; measure a full FLAG-projection scan.
| Representation | How to produce | Scan command | Capture |
|---|---|---|---|
| Arrow/Parquet (oxbow) | `oxbow` BAM→Arrow, or ADAM `saveAsParquet` with flags-only projected schema | project `flag`, feed kernel | retrieval MB/s, one-time convert cost |
| polars-bio | `pl.scan_...().select("flag")` (projection pushdown) | count | MB/s, memory |
| Genozip | `genozip $BAM`; `genocat --FLAG` (a **filter**, label as such — not a projection) | time | MB/s |
| fixed-width raw column | `flags.u16` + LZ4/Zstd blocks (2019 emulation, upper bound) | new kernel | GB/s |

**Capture:** projection throughput (MB/s of FLAG to the kernel), **one-time conversion cost** (amortization honesty),
and whether page/dictionary/RLE decode can saturate the kernel (it can't — record the gap).
**Pass criteria (expected ordering):** `BAM ≪ CRAM-proj (E1) < Parquet-proj < fixed-width-SIMD ≈ kernel-bound`.
**Fills:** the new "related formats" paragraph + a comparison table; defuses the ADAM/oxbow/polars-bio rebuttal.

---

## E3. Parallel scatter-gather engine — the end-to-end thesis demo

**Objective:** show that with a cooperating layout + parallelism the pipeline becomes **disk-bound**, and the kernel
stops being the bottleneck — the paper's thesis, demonstrated on real hardware, not emulated.
**Build** `sg_flagstat` (C/C++), architecture:
```
reader(io_uring / O_DIRECT, 1–4 MB compressed FLAG-column blocks)
  → queue  (variant A: single spinlock; variant B: lock-free MPMC ring; variant C: per-worker sharded)
  → N fused workers: decompress(LZ4/Zstd) → new kernel → thread-local flagstat[16][2] (alignas(64))
  → reduction: sum thread-locals (trivial)
```
**Sweeps & captures:**
| Variable | Range | Capture |
|---|---|---|
| N workers | 1 … physcores, then ×2 SMT | throughput GB/s, scaling curve |
| queue design | spinlock / lock-free / sharded | contention delta (GB/s, time-in-lock) |
| codec | none / LZ4 / Zstd | where the bottleneck sits vs §2 ceilings |
| cache | cold (O_DIRECT) vs warm | honesty on page-cache effects |
**Capture also:** bottleneck attribution at each N (disk / decompress / kernel / memory), via `perf` + the §2 ceilings.
**Pass criteria:** with enough fused workers, sustained BW plateaus at the §2 **NVMe read ceiling (~5–7 GB/s)** with the
kernel <100% utilized → "kernel is never the bottleneck once the format cooperates." Report the spinlock-vs-lockfree gap
as a secondary figure (expect the single spinlock to cap scaling first — that contention story is itself a result).
**Fills:** the headline end-to-end figure; the strongest single artifact for the systems angle.

---

## E4. Bitplane FLAG codec, co-designed with the operator — **[novelty / potential new contribution]**

**Objective:** the concrete answer to "what encoding is missing." Store FLAG **bit-transposed** into 16 dense bitplanes;
then the *positional* popcount collapses to 16 ordinary popcounts over 1-bit/read bitmaps, and each plane (mostly all-0
or all-1) is trivially compressible — and constant runs can be counted **without decompressing**.
**Build** a transform + codec:
```
u16 FLAG array  --bit-transpose-->  16 × bitmap[n bits]   (SoA by bit position)
each plane: RLE / Roaring / bit-pack + Zstd ; count = Σ popcount(plane_j)
```
**Arms & captures:**
| Arm | Capture |
|---|---|
| raw u16 + LZ4/Zstd (2019 baseline) | size, decode+count GB/s |
| bitplane + Zstd | size, decode+count GB/s |
| bitplane + RLE/Roaring, **count-without-materialize** on constant runs | size, GB/s, % runs skipped |
**Metrics:** compressed size vs 2019; decode+count throughput; fraction of input skipped via run-counting.
**Pass criteria:** smaller *and* faster than raw+Zstd on real FLAG (FLAG is extremely low-entropy — a handful of values
dominate), demonstrating a layout co-designed with the operator. If it wins clearly, this is arguably its own short
paper / the strongest "format primitive" evidence.
**Fills:** the "missing encoding primitive" contribution + the GA4GH/hts-specs ask; the E4 table/figure.

---

## E-C. Correctness gate (run before timing anything)

**Objective:** credibility floor — the new kernel must reproduce `samtools flagstat` exactly.
```bash
samtools flagstat $FILE > ref.txt
./bench --flagstats -i flags.u16 > new.txt   # (or the E1/E3 path)
diff <(normalize ref.txt) <(normalize new.txt)   # must be identical for all 2×13 categories
```
Also differential-test the kernel vs a scalar oracle across offsets, zero, all-ones, alternating, random (as Paper #1
already does). **Pass:** byte-identical counts on every readset. Oracle-failed runs are reported as skipped, not timed.

---

## 8. Consolidated data-collection manifest (tick before rewriting)

Each row is a number the rewrite needs and where it goes.

| # | Datum | Experiment | Paper element it fills | Status |
|---|---|---|---|---|
| D1 | samtools/htslib version string | §1.2 | Methods (kills "1.9 straw man") | `[PENDING]` |
| D2 | modern-CPU kernel table (I/w, C/w, Br.miss, speedup) | E-K | Table `results-simulated` | `[PENDING]` |
| D3 | new-vs-2019 kernel Δ | E-K | Intro reposition + Paper #1 cite | `[PENDING]` |
| D4 | memcpy-crossover point on modern CPU | E-K/§2 | Fig `perf-cannonlake` | `[PENDING]` |
| D5 | roofline ceilings (disk, decompress/core, memcpy, pure-load) | §2 | new roofline panel | `[PENDING]` |
| D6 | CRAM 4-arm timings + decode:count split | E1 | replaces emulated table; §2 framing | `[PENDING]` |
| D7 | columnar comparison (Parquet/polars/Genozip/fixed-width) MB/s + convert cost | E2 | related-formats table | `[PENDING]` |
| D8 | scatter-gather scaling curve + queue-design delta + bottleneck attribution | E3 | headline end-to-end figure | `[PENDING]` |
| D9 | bitplane codec size + decode+count GB/s + %-skipped | E4 | "missing primitive" table | `[PENDING]` |
| D10 | correctness diff (byte-identical) | E-C | one sentence in Results/Methods | `[PENDING]` |
| D11 | rebuilt Table `experimental-datasets` for the modern file | §1.3 | Table 4 | `[PENDING]` |
| D12 | 2023–2025 deluge stats (UKB 500k WGS, All of Us 414k, $/genome) | (lit, done) | Intro motivation | ✅ in PAPER_CHANGES |

---

## 9. Decision gates — what's required before the rewrite

- **MINIMAL SET (green-lights the rewrite): E-C + E-K + E1.** Correctness, a modern-hardware kernel number, and a real
  CRAM `required_fields` baseline. This alone makes the paper honest, current, and non-embarrassing. Effort: ~1–2 days
  once a modern box + one CRAM are in hand.
- **STRONG SET (adds a genuine new result): + E3.** The end-to-end parallel demo is the best single figure.
- **NOVEL SET (justifies more than a revision): + E4.** The bitplane codec is the one experiment that could stand as its
  own contribution.
- **SKIP for a PR post:** re-running the full 700–56,000× TB-scale table against multithreaded `samtools flagstat -@`.
  Reframe those as an explicit conditional upper bound (they shrink under modern multithreaded htslib and were never the
  defensible headline).

**Do not start rewriting `main.tex` until the MINIMAL SET is green.** The text edits in `PAPER_CHANGES.md` §4(A) are the
only work that can proceed in parallel with data collection, because they need no new numbers.
