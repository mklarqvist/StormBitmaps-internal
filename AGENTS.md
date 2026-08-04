# AGENTS.md

Working rules for this repository. Applies to humans and coding agents alike.

## Read these first, in this order

| Document | What it is | Authority |
|---|---|---|
| **`PROBLEM_STATEMENT.md`** | **The research objective.** Start here, always. | **Highest — wins all conflicts** |
| `RESEARCH_PLAN.md` | Phases, gates, kernel taxonomy, benchmark protocol, project sequence | Subordinate to the problem statement |
| `LANDSCAPE.md` | Prior art, competitive landscape, known defects | Reference. Its §7 predates the pairing-matrix reframing |

Do not propose a design, write a kernel, or add a benchmark without having read
`PROBLEM_STATEMENT.md`. It is short and it is the whole point.

## The objective in one paragraph

Compute all-pairs set-intersection cardinality `|Xᵢ ∩ Xⱼ|` by selecting, **per pair and per chunk,
the best-matched pair of representations** — bitmap (B), sorted array (S), RLE/run (R), WAH fills
(W), Roaring (Ro) — and vectorizing each cell of that pairing matrix. Bitmap × bitmap costs Θ(m)
*regardless of density*; on realistically skewed data most pairs have a near-empty side, so most of
that cost is spent ANDing zeros. The available win is **10²–10⁵×, asymptotic**. Faster popcount is
not the point; popcount is already solved.

## Mandatory reference lookups

Four skills are wired into this repo at `.claude/skills/` (symlinked into the shared
`personal-skills` repo). **Using them is not optional.** This is a SIMD kernel project; every
intrinsic choice, ISA gate, and timing claim must be sourced, not recalled.

| Skill | Authority for | **Consult before** |
|---|---|---|
| `lookup-intel-intrinsics` | x86 intrinsic prototypes, headers, CPUID gates, instruction/XED mappings, Intel-published latency & throughput (CPI) | writing or changing *any* SSE/AVX2/AVX-512 intrinsic; citing any x86 port, latency, or throughput number; claiming an ISA feature gate |
| `lookup-neon-intrinsics` | ACLE prototypes, vector types, AArch64 instruction mappings, architecture/extension gates (`+sha3`, `+dotprod`, `+i8mm`, SVE2), lane/immediate constraints | writing or changing *any* NEON/SVE2 intrinsic; claiming an intrinsic is available on a target core. **Carries no timing data** |
| `lookup-asimd-firestorm` | Apple Firestorm execution resources, scheduling, measured latency/throughput | any Apple M-series performance claim; choosing unroll or accumulator counts on Apple silicon |
| `optimize-aarch64-kernel` | Disassembly-driven AArch64 optimization workflow, `llvm-mca`, differential correctness, on-target measurement | any AArch64 kernel optimization pass |

### The evidence ladder

Every performance number in this repo sits at exactly one of three tiers, and **must be labelled
with its tier**:

1. **Recalled** — from model memory. **Never citable.** Not in docs, not in comments, not in the
   paper. If you catch yourself writing a port number or a cycle count without having opened a
   source, stop and do the lookup.
2. **Sourced** — from the local corpora (Intel Intrinsics Guide, ACLE, `applecpu`). Citable as a
   *published or vendor-measured* figure, and must be attributed as such.
3. **Measured** — by us, with `perf`/`llvm-mca`/benchmark on the target. The only tier that
   supports a claim about *our* kernel.

Tier 2 never substitutes for tier 3 on a throughput claim. Rule 8 below is the same rule stated
from the other direction.

### Open items in this repo that require a lookup

These are currently tier-1 (recalled) and must be raised to tier 2 before they appear in any
write-up, then tier 3 before they appear in the paper:

- `RESEARCH_PLAN.md` §3.3 — `VPOPCNTQ` on port 5 at 1/cycle; the derived **0.125 cycles/word**
  ceiling; port assignments for `VPANDQ` / `VPADDQ` → `lookup-intel-intrinsics`
- `RESEARCH_PLAN.md` §3.3 — the Harley-Seal/CSA "net loss on AVX-512" argument. It rests entirely
  on those port assignments; re-derive once sourced
- `RESEARCH_PLAN.md` §4 D1 — `VPGATHERDD` / `VPGATHERQQ` throughput → `lookup-intel-intrinsics`
- `RESEARCH_PLAN.md` §4 D3 — `VPCONFLICTD` semantics and cost → `lookup-intel-intrinsics`
- `RESEARCH_PLAN.md` §3.2 — register-file budgets per ISA (32 ZMM / 16 YMM / 32 Z-regs) and the
  resulting MR×NR limits → `lookup-intel-intrinsics`, `lookup-neon-intrinsics`
- Phase 7 cross-ISA ports — every NEON/SVE2 intrinsic and its architecture gate →
  `lookup-neon-intrinsics`; Apple-core scheduling → `lookup-asimd-firestorm`

The corpora are local and fast (4,703 NEON pages, 7,151 Intel pages, 6,256 `applecpu` files).
Consult them **before** searching the web for anything they cover.

## Standing rules

1. **Scope is kernels and algorithms.** No application layer, no domain integration, no genotype
   encodings, no file formats. Genome-wide LD is the *follow-on* project (`RESEARCH_PLAN.md` §8b),
   sequenced strictly after this one.

2. **Asymmetric cells are the priority.** B×S, B×R, S×R (P0 in `RESEARCH_PLAN.md` §1) carry the
   real-world benefit. B×B register blocking is P2 — it is ~1% of pairs on skewed data. Do not
   reorder this on the grounds that B×B is easier or more familiar.

3. **Never inflate a sparse side to a bitmap to reuse the B×B kernel.** That is the naive fallback
   that recovers *zero* of the available saving, and beating it is claim P3. It is permitted only
   as an explicitly labelled benchmark baseline.

4. **Every benchmark reports both a uniform and a 1/i cardinality spectrum.** Uniform-density data
   makes this project's entire contribution invisible. The gap between the two spectra *is* the
   result. See `RESEARCH_PLAN.md` §7.2.

5. **State cache residency with every throughput number.** The legacy README quotes 114 GB/s —
   above that machine's DRAM bandwidth — with the `N < 256,000` qualifier buried in prose. Do not
   reproduce that framing.

6. **Every kernel is differential-tested against the scalar oracle**, on every ISA you can reach,
   at every blocking factor. `tests/test_storm.c` is the harness; run it via `ctest`. A test that
   cannot fail is worthless — when you fix a bug, revert the fix and confirm the suite goes red
   before you trust it.

7. **Selection must be near-free.** At N² pairs an expensive per-pair decision eats the saving it
   exists to unlock. Decisions come from O(1) precomputed metadata or are hoisted to tile
   granularity. This is gated (P2, Gate 1) before any SIMD is written.

8. **Derived numbers are marked as derived until measured.** The ~0.125 cycles/word ceiling, the
   port assignments, and the Harley-Seal loss argument are all analytic estimates. Raise them to
   *sourced* via `lookup-intel-intrinsics`, then to *measured* via `perf` counters, before citing
   any of them. See the evidence ladder above.

9. **Baselines are tuned in good faith.** Same flags, same alignment, same warmup. A rigged
   baseline is the fastest way to lose a reviewer, and CRoaring's maintainer is a collaborator on
   this repo's prior work.

10. **Results are data, not prose.** Every figure regenerates from `results/*.json` by script. No
    hand-curated numbers anywhere.

## Language and ABI

The library is **C++17 internally** (`storm.cpp`) with a **pure C ABI** (`extern "C"` in
`storm.h`). Rules:

1. **Never let anything escape the `extern "C"` boundary that C cannot represent.** A C++
   exception unwinding through an `extern "C"` frame is undefined behaviour. Since C++ internals
   may now allocate (`std::vector` throws `std::bad_alloc`), every public entry point must be
   exception-tight: catch at the boundary and return an error code. There is no exception today —
   keep it that way.
2. **The public header must remain C-includable.** No templates, no `class`, no default arguments,
   no references in `storm.h`'s `extern "C"` block. C++ lives in `storm.cpp` and future internal
   headers, not the public surface.
3. **`tests/test_storm.c` stays C.** It is compiled by the C compiler and linked against the
   C++ objects, which makes it the ABI regression test — if the boundary breaks, it fails to link.
   Verify with `nm`: 42 unmangled `STORM_` exports, zero `__Z` symbols.
4. **C++17 is for compile-time specialisation** of the pairing-matrix kernels — `template<Repr A,
   Repr B, int MR, int NR>` with `if constexpr`. That is the reason for the switch; do not
   introduce runtime polymorphism (virtuals, `std::function`) into hot paths.

## Known-broken code

All 13 Phase 0 defects are fixed and covered by regression tests — see `LANDSCAPE.md` §8 for the
record, including how each fix was verified. What remains:

- **`libalgebra` carries an uncommitted local fix.** `libalgebra/libalgebra.h` guarded
  `#include <x86intrin.h>` on `_MSC_VER` (a *compiler* test, not an architecture test), so arm64
  never compiled. The fix lives in the submodule working tree only — the pin is still upstream
  `bff182e`, so **a fresh clone does not build on arm64**. Needs pushing to
  `mklarqvist/libalgebra` (or a private fork) and a pin bump. See `LANDSCAPE.md` §8.2.

## Conventions

- **C++17 implementation, C ABI** (see Language and ABI above). `storm.cpp` + `storm.h`.
  Apache-2.0 (already correct — keep it).
- Tests are compiled as **C11** so they exercise the C ABI. CRoaring forces C11 anyway
  (`<stdatomic.h>`), and `CMAKE_C_STANDARD` is inherited by `add_subdirectory()`.
- No mandatory dependencies beyond `libalgebra`. CRoaring is vendored at `third_party/CRoaring`
  (pinned **v4.7.2**) and needed only by `benchmark`; the library and tests build without it.
- Public symbols keep the `STORM_` prefix.
- `-march=native` by default, defeatable with `-DSTORM_DISABLE_NATIVE=ON`.
- Runtime ISA dispatch, not build-time. (BitMagic's compile-time-only selection is a documented
  competitive gap — see `LANDSCAPE.md` §5.)

## Before opening a PR

- [ ] **Every intrinsic used was looked up** (`lookup-intel-intrinsics` / `lookup-neon-intrinsics`)
      — prototype, CPUID/architecture gate, and instruction mapping confirmed
- [ ] **Every performance number carries its tier** — recalled (not allowed), sourced, or measured
- [ ] Differential test against the scalar oracle passes on every ISA you can reach
- [ ] Fuzz target run over `{n_vec, n_words, density, clustering, alignment}`
- [ ] Benchmark results attached for both uniform and 1/i spectra
- [ ] Cache residency stated for every throughput figure
- [ ] No new hardcoded magic thresholds — they belong in the cost model (M4)
