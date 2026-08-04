# Cross-ISA head-to-head vs CRoaring — N1 and N7 resolved

Four microarchitectures, real baseline, 2026-08-04. This closes the two gaps
that made the work unpublishable: **no baseline** and **one ISA**.

| host | CPU | ISA |
|---|---|---|
| `apple-m4` | Apple M4 | NEON |
| `fpga-neo1` | AWS Graviton3-class | ARM Neoverse, **SVE** |
| `fpga-neo2` | AWS Graviton4-class | ARM Neoverse, **SVE2** |
| `fpga-sapphire` | Xeon Platinum 8488C | **Sapphire Rapids**, AVX-512 + VPOPCNTDQ |

GCC 11.5 `-O3 -march=native` on the three Linux hosts, Apple clang 21 on the M4.
CRoaring **v4.7.2** from its own amalgamation.

**Correctness first, per host:** 2,668,209 differential checks on the ARM hosts
and 2,428,809 on x86 (fewer variants exist there), zero failures. Every baseline
is additionally checked against CRoaring **per pair** before it is timed.

**Baseline fairness (standing rule 9).** CRoaring is measured *after*
`roaring_bitmap_run_optimize()` and `shrink_to_fit()` — what a competent user
does, and the configuration that gives it run containers on clustered data. It
is 1.5–2.5× faster than default CRoaring on this data, and it is what every
ratio below is computed against. Reporting against default CRoaring would have
inflated every number.

## Storm vs CRoaring, clustered / 1-over-i spectrum

| density | apple-m4 | neoverse-sve | neoverse-sve2 | sapphire |
|---:|---:|---:|---:|---:|
| 0.0002 | 4.4× | 2.3× | 2.5× | 2.2× |
| 0.001 | 4.0× | 3.1× | 3.5× | 4.0× |
| 0.005 | 8.0× | 3.9× | 4.8× | 5.6× |
| 0.02 | **13.8×** | 6.5× | 6.7× | 7.8× |
| 0.1 | 5.5× | 5.3× | 4.9× | 4.6× |
| 0.4 | 5.4× | 3.0× | 2.6× | 2.7× |
| long runs | 1.7× | 1.9× | 1.7× | 1.9× |

**Storm beats a fairly-tuned CRoaring at every density on every machine.**
Claim **P5 holds, cross-ISA.** The advantage peaks near density 0.02 and falls
off both ways, which is the U-curve of `results/density.png` seen through a
different baseline.

## Storm vs all-bitmap — claim P1

| density | apple-m4 | neoverse-sve | neoverse-sve2 | sapphire |
|---:|---:|---:|---:|---:|
| 0.0002 | 35.5× | 35.2× | 47.3× | **56.4×** |
| 0.001 | 20.5× | 21.7× | 28.9× | 37.5× |
| 0.02 | 4.6× | 5.1× | 6.4× | 8.4× |
| long runs | **55.4×** | **70.4×** | **79.4×** | **91.3×** |

**P1 (≥50× over all-bitmap) holds on run-structured data on all four hosts**
(55–91×) and at the sparse end on three of four (47–56× at density 2e-4). It
does **not** hold in the mid-density band, where the correct answer is that
bitmap × bitmap is already the right kernel — which is the pairing matrix
working, not a failure.

Note the ordering: **the advantage is largest on Sapphire Rapids**, the machine
with the strongest dense kernel. Work avoidance scales with how expensive the
work you avoid is.

## The finding that costs us something

On the dense corpus (d = 0.35, uniform) on Sapphire Rapids, **CRoaring beats
Storm 5×** (173.6 vs 742.1 ns/pair). Cause: Storm has **no AVX-512 kernel**.
Every vector path in `kernels/` is gated on `__ARM_NEON`; on x86 the dense cell
falls back to the portable multi-accumulator scalar loop, and GCC 11.5 does not
turn it into `VPOPCNTQ`. CRoaring's `bitset_container_and_justcard` is
hand-vectorized for AVX-512 and wins accordingly.

This must be reported, not buried. It bounds the claim precisely: **Storm's
contribution is representation selection and work avoidance, and on the one
cell where the work is irreducible it is currently behind the state of the art
on x86.** Writing an AVX-512 B×B kernel is a known, bounded piece of work; until
it exists, the dense cell on x86 is a documented weakness.

## What this resolves

- **N1 — baselines.** Closed. P1 and P5 are now falsifiable and measured.
- **N7 — cross-ISA.** Substantially closed: four microarchitectures including
  SVE, SVE2 and AVX-512, against the plan's target of "≥4 ISAs including one
  ARM" (`RESEARCH_PLAN.md` §11).
- **F1's ISA-locality.** The NEON-derived conclusions were correctly flagged as
  ISA-local; the x86 result confirms that caution was warranted.

## What it does not resolve

- **Gate 1 still fails** — selection costs 28.3% of runtime against a 2% budget
  (M4 section of `OPTLOG.md`). Every number above uses the *best cell per
  corpus*, i.e. an oracle, not the model. The oracle is legitimate for P5
  (it is what a perfect selector would achieve) but P1/P5 as *shipped library
  behaviour* still depend on M3 tile hoisting.
- Corpora remain L2-resident; the 10⁷-bit regime is untested.
- No threading, no §6 batched API.
