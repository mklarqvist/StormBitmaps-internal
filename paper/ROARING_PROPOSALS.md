# Proposals to CRoaring

**Status of this document.** Draft, written to be sent upstream. Every claim about CRoaring below is
checked against the vendored amalgamation at `third_party/croaring_amalg/` (byte-identical to the
release it was taken from) with a file-and-line reference; nothing is recalled. Every performance
number is labelled with how it was obtained. Derivations are in
[`THEORY.md`](THEORY.md) §14, which is self-contained.

---

## 0. The framing, stated first because it determines how everything below should be read

CRoaring's container selection is **the exact optimum of a storage objective**, and it is a
well-chosen objective for a general-purpose library that cannot see its users' workloads.

All three container decisions are the same rule — `argmin` over serialized size:

| decision | rule as written | equivalent to | source |
|---|---|---|---|
| array ↔ bitset | bitset iff `card > DEFAULT_MAX_SIZE = 4096` | `2·card > 8192` bytes | `roaring.h:2486`, `:2621`, `:3443` |
| array → run | run iff `2 + 4·n_runs < 2·card` | `bytes(run) < bytes(array)` | `roaring.c:9929–9941` |
| bitset → run | run iff `2 + 4·n_runs < 8192` | `bytes(run) < bytes(bitset)` | `roaring.c:9963–9975` |

`4096` is not a tuning constant that happens to be 4096. It is `M/e` — the container width in bits
over the bits per array element, `65536/16` — the cardinality at which an array container and a
bitset container occupy the same bytes. It contains no machine constant, no word width, and no
property of the workload, which is precisely what makes it correct as a compile-time constant.

**What we found.** For a *query-cost* objective — specifically `roaring_bitmap_and_cardinality()`,
count-only, no materialised result — the crossover is a different quantity:

```
                 M                                      c_w    M
  τ_storage  =  ───  = 4096 ,        τ_and-card  =  ( ───── )·───  ,
                 e                                     c_p     w
```

where `c_w` is the cost of one vectorised word AND-plus-popcount and `c_p` the cost of one
membership probe into a bitset. The ratio is

```
  τ_storage / τ_and-card  =  (w/e) · (c_p/c_w)  =  4 · (c_p/c_w)   on a 64-bit host.
```

On our test host (Apple M-series, count-only AND) we measure the *symmetric* crossover — the
cardinality at which a scalar array/array merge stops beating a bitset/bitset popcount — at
**`card ≈ 64–128`**, which puts `τ_storage/τ_and-card` at **32–64×**.

*(Measured; `RESEARCH_PLAN.md` §26, one host, being re-measured on three more. Two things are worth
separating: the crossover is the measured input and the factor is derived from it. The symmetric
crossover fixes `c_m/c_w`; reading it as `τ_and-card` additionally assumes a probe costs about two
merge steps, which we have **not** measured. If a maintainer has a view on that ratio we would
rather use theirs.)*

**This is not a claim that 4096 is wrong.** `THEORY.md` §14 Theorem C shows the two constants are
the two limits of a single one-parameter family: introduce a price `λ` per byte and minimise
`query cost + λ · bytes`; the induced threshold `τ(λ)` is monotone in `λ`, equals the compute
crossover at `λ = 0`, and tends to `4096` as `λ → ∞`. Every value in between is optimal for some
price of a byte. `4096` is the unique threshold at which the compute-favourable move is *never* paid
for in bytes — a defensible default for a library, and the one we would also pick if we had to pick
one.

**What we are proposing** is therefore not a new constant. It is (i) making the objective explicit
and selectable, (ii) three self-contained correctness/consistency fixes that are good independent of
any of this, and (iii) two performance items that are pure wins on ARM. We also list what we
evaluated and **rejected**, including one proposal that turned out to already be implemented.

---

## Summary table

| # | Proposal | Change | Risk | Effort | Verdict |
|---|---|---|---|---|---|
| **R1** | Document the objective | docs only | none | ~1 h | **propose** |
| **R2** | Three-way `argmin` in `convert_run_optimize` | ~15 lines | very low | ~2 h | **propose** |
| **R3** | Vectorise the array-container intersection kernels on ARM | new NEON path | low | ~1–2 d | **propose** |
| **R4** | An explicit, opt-in `compute_optimize()` pass | ~40 lines + docs | **format hazard** — see R4.3 | ~1 d | **propose, with a hard caveat** |
| **R5** | Optional lazily-built rank directory on bitset containers | new side structure | medium | ~3–5 d | **propose as an experiment**, not as a fix |
| **R6** | An occupancy summary (zone map) below the container level | substantial | medium | ~1–2 wk | **offer**, with the window that says when it pays |
| **R7** | Threshold derived from a measured cardinality distribution | auto-tuning | high | — | **reject for now** |
| **R8** | A count-only intersection path avoiding materialisation | — | — | — | **reject — already implemented** |
| **R9** | Lower `DEFAULT_MAX_SIZE` globally | — | — | — | **reject — breaks the serialization format** |

---

## R1 — Document that the container rules implement a storage objective

**The change.** Two paragraphs in the container documentation and one comment beside
`DEFAULT_MAX_SIZE`, saying: (i) the three decisions are one rule, `argmin` over serialized size;
(ii) `4096 = M/e` is a property of the format, not a tuned constant; (iii) for query-cost-dominated
workloads the relevant crossover is a different quantity and is machine-dependent.

**Evidence.** The three rules, above, with line references. That they coincide is not stated
anywhere in the source we read; `DEFAULT_MAX_SIZE`'s comment (`roaring.h:2485`) reads
"Containers with DEFAULT_MAX_SIZE or less integers should be arrays", which states the rule but not
the objective it optimises.

**Expected effect.** None on performance. It converts a question users currently answer by
experiment into one they can answer by reading, and it is the precondition for R4 being understood
as a choice rather than a correction.

**Risk.** None.

**Cost of adopting.** An hour, and agreeing the wording.

---

## R2 — Make `convert_run_optimize` a three-way `argmin` (it is currently not confluent)

**The finding.** `convert_run_optimize` performs a *two-way* size comparison, and which two depends
on the container's present type:

- from `ARRAY_CONTAINER_TYPE` (`roaring.c:9929–9941`): convert to run iff `2 + 4r < 2k`;
- from `BITSET_CONTAINER_TYPE` (`roaring.c:9963–9975`): convert to run iff `2 + 4r < 8192`.

It never compares array against bitset (that comparison lives in `DEFAULT_MAX_SIZE`, elsewhere).
Consequently **the same set can end in different representations depending on how it arrived**, and
the outcome can be strictly larger than the three-way minimum.

**Worked instance.** `card = 3000`, `n_runs = 1600`. Sizes: array `6000` B, run `6402` B,
bitset `8192` B.

- arriving as `ARRAY`: `6402 ≥ 6000` → stays `ARRAY` (6000 B) — the three-way optimum;
- arriving as `BITSET`: `6402 < 8192` → becomes `RUN` (6402 B) — 402 B larger.

The disagreement region is exactly `(k−1)/2 ≤ r ≤ 2047` with `k ≤ 4096`.

**The change.** Compute all three serialized sizes and take the `argmin`. Roughly fifteen lines,
reusing `array_container_number_of_runs` / `bitset_container_number_of_runs`, which are already
called on the respective paths.

**Expected effect.** Never larger than today, sometimes smaller, and confluent — `run_optimize()`
becomes a function of the set rather than of the set plus its history. We have not measured a
speedup and do not claim one; this is a consistency fix.

**Risk.** Low, but real: it changes which container type some bitmaps end up in, so it will move
container-census assertions in tests and can change serialized bytes. It cannot change any query
result.

**Why we think it is worth flagging even though the bytes are small.** It is the mechanism behind a
regression we hit ourselves. Lowering the array→bitset threshold *before* `run_optimize()` measured
**46 % worse** on `census1881` (1496 → 2186 ns/pair; measured, single host) precisely because
pre-promoting a borderline container changes which of the two comparisons it is subjected to and can
flip it into `RUN`. Running any such pass *after* `run_optimize()` avoids the interaction entirely —
which is what our own patch does — but a three-way `argmin` removes the trap at the source.

---

## R3 — Vectorise the array-container intersection kernels on ARM

**The finding, verified from source.** In this amalgamation the array/array intersection kernels are
vectorised only under `CROARING_IS_X64`:

```c
// roaring.c:6992–7016, array_container_intersection_cardinality
    if (card_1 * threshold < card_2) { ... intersect_skewed_uint16_cardinality ... }
    else if (card_2 * threshold < card_1) { ... }
    else {
#if CROARING_IS_X64
        if (croaring_hardware_support() & ROARING_SUPPORTS_AVX2) {
            return intersect_vector16_cardinality(...);
        } else { return intersect_uint16_cardinality(...); }
#else
        return intersect_uint16_cardinality(...);      // scalar on ARM
#endif
    }
```

`array_container_intersection` (`roaring.c:6951`) has the same shape.

Meanwhile the **bitset** container paths *are* NEON-vectorised on ARM:
`bitset_container_compute_cardinality` (`roaring.c:7531`, `#elif defined(CROARING_USENEON)`) and the
whole `CROARING_BITSET_CONTAINER_FN` family including `bitset_container_and_justcard`
(`roaring.c:8084`).

**Why this matters more than it looks.** The array/bitset compute crossover is
`τ = (c_w/c_p)·(M/w)`. On ARM the numerator `c_w` is a NEON word AND-plus-popcount while the array
side is scalar, so `c_w/c_p` is smaller than on an AVX2 x86 host and **`τ` sits lower on ARM than on
x86 by the array-side vectorisation ratio.** Adding a NEON array path raises `τ` back toward its x86
value, which is the direction that keeps arrays useful.

**The change.** A NEON implementation of `intersect_uint16_cardinality` (and, for symmetry,
`intersect_uint16`), guarded by `CROARING_USENEON`, following the existing shuffle-compare structure
of `intersect_vector16_cardinality`. The `uint16x8_t` comparison-and-count formulation is
straightforward; the galloping and skewed paths are unaffected and should stay scalar.

**Expected effect.** Faster array/array intersection on ARM. We have not implemented or measured it
and quote no factor.

**Risk.** Low. It is an additional code path behind an existing feature macro, exercised by the
existing intersection tests. The usual caveat about tail handling and reading past the end of the
allocation applies — the x86 path grows the output by `sizeof(__m128i)/sizeof(uint16_t)`
(`roaring.c:6959`) and a NEON path needs the equivalent.

**Cost of adopting.** One to two days including tests, plus whatever ARM CI already exists.

---

## R4 — An explicit, opt-in pass that retunes containers for query cost

**The change.** A companion to `run_optimize()`:

```c
/* Retune container representations for query cost rather than storage.
 * Runs AFTER run_optimize(). Promotes array containers above `threshold`
 * to bitsets. Pass -1 for a built-in default. IN-MEMORY ONLY: see below. */
bool roaring_bitmap_compute_optimize(roaring_bitmap_t *r, int32_t threshold);
```

We have this implemented and measured as `roaring_bitmap_storm_promote_arrays()`
(`third_party/croaring_modified/roaring.c:16402–16421`, twenty lines). It is a pure representation
change: the encoded set is unchanged, so every query must return an identical answer before and
after. We verify that on every pair against an independent implementation before any timing is
taken; zero disagreements on any run.

### R4.1 Evidence

Measured, `roaring_bitmap_and_cardinality` over all pairs, one host, threshold 64, applied after
`run_optimize()`. **These are being re-measured on three further hosts and should be treated as
directional.**

| corpus | density | stock (ns/pair) | with the pass (ns/pair) | change |
|---|---:|---:|---:|---:|
| `census1881` | `1.2e-3` | 1483.5 | **76.1** | **19.5×** |
| `census-income` | `1.7e-1` | 4873.1 | **667.3** | **7.3×** |
| `wikileaks-noquotes` | `1.0e-3` | 449.9 | 422.3 | 1.1× |
| `dbpedia-link` | `1.6e-6` | 137.1 | 130.7 | 1.0× |
| `uscensus2000` | `8.1e-7` | 16.4 | 21.6 | **0.8×** |
| `as-skitter` | `9.1e-6` | 31.3 | 48.4 | **0.6×** |

### R4.2 Why the sign flips, and why that is the interesting part

**The gain is large where containers are already dense and it is a loss where they are sparse.** A
work model does not predict the loss: promoting a container above the crossover reduces the item
count of every pair it participates in. The loss is a footprint effect. Promoting a container of
cardinality `k` multiplies its bytes by

```
  M / (e·k)  =  4096 / k ,
```

which is `64×` at `k = 64` and `1×` at `k = 4096`. On a sparse corpus most containers sit just above
the threshold, so the pass inflates the resident working set by a large factor while removing only a
handful of merge steps per pair — and the inflation raises the effective cost of *every* bitset
operation, including the ones the pass did not touch.

**This is the second reading of `4096`, and it is the one that makes CRoaring's choice look best:
`4096` is the unique threshold at which promotion never inflates the working set.** Any lower
threshold buys operations with bytes at an exchange rate of `4096/k − 1`. That exchange rate is
exactly the `λ` of Theorem C, and the honest conclusion is that **neither `64` nor `4096` is right
across the density range** — which is why R4 is proposed as an opt-in pass with a caller-supplied
threshold and not as a change of default.

### R4.3 The hard caveat — `4096` is part of the serialization format

**This is the most important thing in the document and we would not have proposed R4 without it.**

The portable serialization format records only `(key, cardinality − 1)` plus a run bitmap; the
array-versus-bitset distinction is *inferred from the cardinality on read*:

```c
// roaring.c, ra_portable_deserialize
    uint32_t thiscard = tmp + 1;
    bool isbitmap = (thiscard > DEFAULT_MAX_SIZE);
```

`ra_portable_serialize` (`roaring.c:14299`) writes each container in its own format via
`container_write`. So a bitset container with `cardinality ≤ 4096` writes 8192 bytes and is read
back as an array container of `cardinality` `uint16`s. **A bitmap carrying promoted containers does
not round-trip.**

Relatedly, `bitset_container_validate` rejects such a container outright:

```c
// roaring.c:8262
    if (v->cardinality <= DEFAULT_MAX_SIZE) {
        *reason = "cardinality is too small for a bitmap container";
        return false;
    }
```

So `roaring_bitmap_internal_validate` will fail on a promoted bitmap.

**Consequences, and they shape the proposal:**

1. `DEFAULT_MAX_SIZE` cannot be changed as a tuning parameter. It is a format constant. Any
   documentation added under R1 should say so in as many words — we believe this is not currently
   stated anywhere a user would find it.
2. A compute-oriented representation can only be a **runtime, in-memory, non-persisted** decision,
   applied after load and never serialized. That is the correct shape for `compute_optimize()` and
   it is what our implementation does.
3. If the maintainers want a persistable form, that is a format-version question (a per-container
   type tag rather than an inferred one) and is far outside what we are proposing.
4. `bitset_container_validate`'s cardinality check is a *storage* invariant, not a correctness one.
   If R4 is adopted, either it is relaxed for in-memory bitmaps or `compute_optimize()` must be
   documented as making `internal_validate` inapplicable. We have no view on which; it is the
   maintainers' invariant.

### R4.4 Risk and cost

**Risk.** Memory inflation, bounded by `4096/threshold` per promoted container; the serialization
hazard above, which must be handled by documentation at minimum; and a new public entry point to
support. It cannot change any query result.

**Cost of adopting.** About forty lines plus documentation and a test that the pass is
answer-preserving on a corpus. The `after run_optimize()` ordering is load-bearing and should be
enforced or at least documented (see R2).

---

## R5 — An optional, lazily-built rank directory on bitset containers

**The observation.** `run_bitset_container_intersection_cardinality` (`roaring.c:10901`) loops over
the runs calling `bitset_lenrange_cardinality` (`roaring.h:1780`), which popcounts `⌈len/64⌉ + O(1)`
words per run. Summing over runs, the cost is

```
  Σ_i ⌈len_i / w⌉ + r   ≈   k/w + r      words,
```

i.e. **a function of the total run length**. With an `O(1)` rank index over the bitset container the
same quantity is

```
  |A ∩ B|  =  Σ_i ( rank_A(v_i) − rank_A(u_i) )       —   exactly 2r lookups,
```

**independent of the run lengths**. The gain factor is `1 + L̄/w` where `L̄ = k/r` is the mean run
length — unbounded as runs get longer.

**The structure.** A bitset container is 1024 words. A directory of 1024 `uint16` prefix counts
(one per word) is 2 KiB, a 25 % overhead, and makes rank one directory load plus one masked
popcount. It would have to be built lazily, discarded on any mutation, and never serialized.

**Why this is proposed as an experiment and not as a fix.** The comparison is
`2·c_ρ·r` against `c_w·(k/w + r)`, so rank wins iff

```
  L̄  =  k/r   >   w · ( 2·c_ρ/c_w − 1 ) ,
```

and `c_ρ/c_w` — a random directory-plus-word access against a vectorised sequential word — **has not
been measured, by us or, as far as we can tell, by anyone.** The condition is derived; the benefit
is not. A run container with a small number of long runs against a bitset is the case that would
benefit, and it is a common shape (sorted identifier ranges, census-style attributes). The
degenerate cases are already special-cased (`run_container_is_full`, `roaring.c:10904`).

**Risk.** Medium: a side structure with an invalidation rule is a new class of bug, and 25 % on
bitset containers is not free. We would not propose it without a measurement first, and we are
offering the derivation rather than the change.

---

## R6 — An occupancy summary below the container level

**Why we are proposing this at all.** The obvious response to everything above is "if your extra
layer is what is winning, contribute the extra layer". That is correct, and this is it.

**The change.** Optionally attach to a bitset container a bit-per-bin occupancy summary at bin width
`W_z` bits (`M/W_z` bits per container; at `W_z = 512`, 128 bits = 16 bytes, a 0.2 % overhead), and
have `bitset_container_and_justcard` AND the two summaries first, visiting only the bins the
summary does not prove empty.

**When it pays, derived rather than asserted.** Under an independent-placement model, with the
summary pass charged a fraction `c/W_z` of a full container scan, the summary strictly beats *both*
a plain scan and a sorted-array enumeration exactly when

```
  x·e^{−x}  >  c/w ,          x = W_z · d   ( = expected set bits per bin ),
```

which is a window in bits per bin and therefore independent of `W_z` and of the universe. At `c = 2`,
`w = 64` the window is `x ∈ [0.032, 5.09]`. Below it, reading the summary costs more than
enumerating the operands; above it no bin is empty and the summary proves nothing. Derivation and
verification in [`THEORY.md`](THEORY.md) §7 (Propositions 11b–11c).

**Two design facts that fall out of the same algebra and are worth having even if the change is not
taken:**

1. Conditioned on being occupied, a bin's expected local density is `d/p` with
   `p = 1 − (1−d)^{W_z}`, which tends to `1/W_z` as `d → 0`. **A summary cannot hand a kernel
   anything sparser than one bit per bin**, so the bin width and the array/bitset crossover are
   governed by the same constant: at `W_z = (c_p/c_w)·w` a singly-occupied bin sits exactly at the
   crossover. Choosing `W_z` above that hands the kernel bins it should enumerate.
2. Because the window has a lower edge, an *unconditional* summary is a net loss on sparse data. Any
   such feature must be gated, and the gate is computable in advance from `W_z` and `w`.

**Expected effect.** Modelled, on structureless data, against a container threshold already set to
the compute crossover, the peak advantage is `(c_p/c_w)·w / (2√(c·W_z))`, valid for
`W_z ≤ (c_p/c_w)·w` and saturating above it. At `c_p/c_w = 16` — our measured merge-step ratio times
an *assumed* probe-to-merge-step ratio of 2, which we have not measured — `w = 64`, `c = 2`:
about `32×` at `W_z = 128`, `16×` at `W_z = 512`, and `11×` for any
`W_z ≥ 1024`, attained at in-container densities of order `10⁻⁴` to `10⁻³`, and below `1×` outside
the window. This is a model of **operations**, not of time; our own measurements of an ungated
summary show a memory-system penalty a work model does not express, which is the reason for the gate.

**Risk.** Medium and honest: it is a new per-container side structure with an invalidation rule, it
must be gated or it loses on sparse data, and it changes the container ABI if attached rather than
kept alongside. It is a bigger ask than anything else here.

---

## R7 — Threshold derived from a measured cardinality distribution — **rejected for now**

**The idea.** Have `run_optimize()`/`compute_optimize()` inspect the bitmap's own container
cardinality distribution (`roaring_bitmap_statistics()` already computes most of it) and pick a
threshold from it.

**Why we are not proposing it.** The derivation says the optimal threshold is

```
                β·c_w·n  +  (1−β)(c_p − c_m)·k̄
  τ*(β, k̄)  =  ───────────────────────────────
                     β·c_p  +  (1−β)·c_m
```

where `β` is the fraction of a container's *partners* that are bitsets and `k̄` the mean cardinality
of its array partners. Two things follow, and both are problems for auto-tuning:

1. **`β` is a property of the query workload, not of the bitmap.** A bitmap's own cardinality
   distribution is not `β` unless it is only ever intersected with bitmaps like itself. A library
   cannot see the pairing distribution at `optimize()` time.
2. **The rule is self-referential.** The partners are containers subject to the same rule, so an
   optimal fixed threshold is a *fixed point* `τ = τ*(β(τ), k̄(τ))`. A fixed point exists but need
   not be unique, and a heuristic that iterates toward one can oscillate between bitmaps in a
   corpus that are optimized at different times.

Auto-tuning against a statistic that is not the governing one is worse than a documented constant.
**What we would propose instead is R4 with an explicit threshold parameter**, so a caller who *does*
know their workload can supply it, and a caller who does not gets today's behaviour.

---

## R8 — A count-only intersection path avoiding materialisation — **rejected, already implemented**

We evaluated this and it is already there, completely. `roaring_bitmap_and_cardinality`
(`roaring.c:17862`) merges the high–low containers and calls `container_and_cardinality`
(`roaring.h:6000`), which dispatches to a non-materialising kernel for **all nine** container pairs:

`bitset_container_and_justcard`, `array_container_intersection_cardinality`,
`run_container_intersection_cardinality`, `array_bitset_container_intersection_cardinality`,
`run_bitset_container_intersection_cardinality`, `array_run_container_intersection_cardinality`.

The bitset/bitset kernel has AVX-512, AVX2, NEON and scalar variants (`roaring.c:7654`, `:7816`,
`:8084`, `:7927`). Nothing is allocated and nothing is written.

Two smaller observations in the same area, offered without a proposal attached:

- `roaring_bitmap_or_cardinality`, `andnot_cardinality` and `xor_cardinality` (`roaring.c:17900–17922`)
  are all computed from `and_cardinality` by inclusion–exclusion. That is exactly right, and it means
  the *cardinality* query has no operation dimension at all — a useful thing to know when reasoning
  about container choice, because it collapses four decisions into one.
- `bitset_container_and_justcard` has no early exit and scans all 1024 words unconditionally. That is
  correct for a count and we are not suggesting otherwise; it is the fixed cost that makes the
  crossover in R4 exist.

---

## R9 — Lower `DEFAULT_MAX_SIZE` globally — **rejected**

Two independent reasons, either of which is sufficient.

1. **It is a format constant, not a tuning constant** (R4.3). Lowering it changes how existing
   serialized bitmaps are interpreted on read.
2. **We tried it and it measured worse.** Lowering the threshold used in `container_add`'s
   `array_container_try_add` path, so that promotion happens *before* `run_optimize()`, measured a
   **46 % regression** on `census1881` (1496 → 2186 ns/pair; measured, single host) — because
   `convert_run_optimize` is not confluent (R2) and pre-promotion changes which size comparison a
   borderline container is subjected to.

The working form is a separate pass applied **after** `run_optimize()`, which is R4.

---

## What we would ask for, in priority order

1. **R1** — the documentation, because it is free and it is the precondition for the rest being read
   correctly.
2. **R2** — the three-way `argmin`, because it is small, strictly non-regressive in bytes, and it
   removes a trap we fell into.
3. **R3** — the ARM array kernels, because it is a pure win with no interface consequences.
4. **R4** — the opt-in pass, *if* the maintainers are comfortable with a documented in-memory-only
   representation change. We would understand a decision not to add public API for it; the twenty
   lines are easy for a user to carry themselves once R1 explains why they might want to.
5. **R5**, **R6** — offered as derivations and experiments, not as patches. We would rather measure
   them properly first, and would be glad of a maintainer's view on whether either is worth the
   invalidation machinery.

**And the thing we would most like to be told we are wrong about:** whether `c_p/c_w` — a membership
probe against a vectorised word AND-plus-popcount — is stable enough across the ISAs CRoaring
targets for `τ_and-card` to be worth naming at all, or whether the honest answer is that a
query-cost threshold is irreducibly per-host and therefore belongs in the caller rather than in the
library. Our measurements are on one microarchitecture and we are not confident either way.
