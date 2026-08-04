# CLAUDE.md

**See [`AGENTS.md`](AGENTS.md) for the full working rules.** This file mirrors the essentials so
it is useful standalone.

## Read before doing anything

1. **[`PROBLEM_STATEMENT.md`](PROBLEM_STATEMENT.md)** — the research objective. **Highest
   authority; wins all conflicts.** Read it before proposing a design, writing a kernel, or adding
   a benchmark.
2. [`RESEARCH_PLAN.md`](RESEARCH_PLAN.md) — phases, gates, kernel taxonomy, benchmark protocol,
   and the Storm → Tomahawk project sequence (§8b).
3. [`LANDSCAPE.md`](LANDSCAPE.md) — prior art, competitors, known defects. Its §7 predates the
   pairing-matrix reframing; the prior-art findings still stand.
4. [`AGENTS.md`](AGENTS.md) — the ten standing rules and the PR checklist.

## The objective

Compute all-pairs set-intersection cardinality `|Xᵢ ∩ Xⱼ|` by selecting, **per pair and per chunk,
the best-matched pair of representations** — bitmap (B), sorted array (S), RLE/run (R), WAH fills
(W), Roaring (Ro) — then vectorizing each cell of that pairing matrix.

Bitmap × bitmap costs Θ(m) *regardless of density*. On realistically skewed data most pairs have a
near-empty side, so most of that cost is spent ANDing zeros. The win is **10²–10⁵×, asymptotic**.
Popcount is already solved (Muła/Kurz/Lemire 2018) — making it faster is not the point.

## Mandatory reference lookups — not optional

Four skills are wired in at `.claude/skills/`. This is a SIMD kernel project: **every intrinsic
choice, ISA gate, and timing claim must be sourced, not recalled.**

| Skill | Consult before |
|---|---|
| `lookup-intel-intrinsics` | writing/changing any SSE/AVX2/AVX-512 intrinsic; citing any x86 port, latency, or throughput number; claiming a CPUID feature gate |
| `lookup-neon-intrinsics` | writing/changing any NEON/SVE2 intrinsic; claiming availability on a target core. *No timing data in this corpus* |
| `lookup-asimd-firestorm` | any Apple M-series performance claim; unroll/accumulator counts on Apple silicon |
| `optimize-aarch64-kernel` | any AArch64 kernel optimization pass |

Local and fast — 4,703 NEON pages, 7,151 Intel pages, 6,256 `applecpu` files. **Use them before
searching the web** for anything they cover.

### Evidence ladder — label every performance number with its tier

1. **Recalled** (from memory) — **never citable.** Not in docs, not in comments, not in the paper.
   Writing a port number or cycle count without opening a source is a defect.
2. **Sourced** (local corpora) — citable as a *published/vendor* figure, attributed as such.
3. **Measured** (`perf`/`llvm-mca`/benchmark on target) — the only tier that supports a claim about
   *our* kernel. Tier 2 never substitutes for tier 3 on throughput.

**Currently tier-1 and must be raised before use:** the `VPOPCNTQ` port-5 / 1-per-cycle assumption
and the derived **0.125 cycles/word** ceiling (`RESEARCH_PLAN.md` §3.3); the Harley-Seal/CSA "net
loss" argument that depends on it; `VPGATHERDD` throughput (§4 D1); `VPCONFLICTD` cost (§4 D3);
per-ISA register-file budgets (§3.2). `AGENTS.md` has the full list.

## The rules that get broken most often

- **Scope is kernels and algorithms.** No applications. Genome-wide LD is the follow-on project
  (Tomahawk), sequenced strictly after this one.
- **Asymmetric cells first.** B×S, B×R, S×R carry the benefit. B×B register blocking is ~1% of
  pairs — do not promote it because it is more familiar.
- **Never inflate a sparse side to a bitmap** to reuse the B×B kernel. That recovers zero saving
  and is precisely what we must beat. Permitted only as a labelled baseline.
- **Every benchmark reports both uniform and 1/i cardinality spectra.** Uniform data makes the
  whole contribution invisible; the gap between the two *is* the result.
- **State cache residency with every throughput number.**
- **Selection must be near-free** — O(1) metadata or tile-hoisted. Gated before any SIMD is written.
- **Never cite a recalled performance number.** Look it up, then measure it. See the evidence
  ladder above — the ~0.125 c/word ceiling and the Harley-Seal loss argument are analytic, not
  measured, and not yet even sourced.

## Language and ABI

**C++17 internals** (`storm.cpp`), **pure C ABI** (`extern "C"` in `storm.h`).

- Never let a C++ exception unwind through `extern "C"` — that is UB. Public entry points must be
  exception-tight; catch at the boundary and return an error code.
- `storm.h` must stay C-includable: no templates, classes, references, or default arguments in the
  `extern "C"` block.
- `tests/test_storm.c` stays **C** on purpose — it links against the C++ objects and is therefore
  the ABI regression test. Check with `nm`: 42 unmangled `STORM_` exports, zero `__Z`.
- C++17 exists here for **compile-time specialisation** of pairing-matrix kernels
  (`template<Repr A, Repr B, int MR, int NR>` + `if constexpr`). No virtuals or `std::function`
  in hot paths.

## State of the code

All 13 Phase 0 defects are fixed, each verified by reverting it and confirming the tests fail.
`tests/test_storm.c` has 1,279 checks against an independent oracle, wired into CTest. Full record
in `LANDSCAPE.md` §8.

Still outstanding: the `libalgebra` arm64 fix lives only in the submodule working tree (pin
unchanged) — see `libalgebra/VENDORED.md`.
