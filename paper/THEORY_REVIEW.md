# Adversarial review of `THEORY.md` and `sections/theory_block.tex`

**Reviewer method.** Every proposition was restated from its hypotheses and re-proved before the
document's proof was read; the document's proof was then compared. Every closed form was then
checked by exhaustive brute force on small cases and by Monte Carlo at scale, using stdlib Python
only. The two scripts are reproduced verbatim in §5 and both run in under 25 s.

**Headline.** The mathematics is, with one exception, correct. Nothing in the numbered results is
fatally wrong. But there are **six statements that claim more than they prove**, one of which is a
**direct self-contradiction with Theorem A**, and one **counterexample family in Theorem B that does
not have the density it is asserted to have** (the theorem itself is true — I verified the range
exhaustively — but the published proof of it does not go through as written). There is also a
**structural gap in the chain to the thesis**: Theorem B is about descriptor *length*, and nothing in
the document proves that any cell's *cost* tracks descriptor length. That link is currently carried
entirely by measurement (claim A7) and the theory presents it as though it were derived.

---

## 1. Verdict table

| # | Result | Verdict | One-line reason |
|---|---|---|---|
| P1 | Fréchet interval, all values attained | **CONFIRMED** | Re-derived; construction `A=[0,a)`, `B=[a−t,a−t+b)` verified exhaustively for `m ≤ 8`, all `(a,b,t)` |
| P2 | width `= min(a,b,m−a,m−b) = m·min(s_A,s_B)` | **CONFIRMED** | Identity verified exhaustively for all `m ≤ 199`, all `(a,b)`; proof step `min(a,b)+m−a−b = m−max(a,b)` is sound |
| P3 | pigeonhole floor; lifts off iff `d_A+d_B>1` | **CONFIRMED** | Immediate from P1; the factor-2 arithmetic (51 % → 2 %, 3.92 % of operand) checks out |
| P4 | `κ_B = ⌈m/w⌉` unconditionally | **CONFIRMED** | Definitional |
| P5 | `O(m·s)` achievability, four cases | **CONFIRMED WITH CORRECTION** | Algorithm exact and probe count `= W_unc` on 4 000 random instances; but the stated hypotheses are both over- and under-specified (§3.1) |
| **Thm A** | headroom ratio `1/(w·s)`, unbounded as `s→0` | **CONFIRMED WITH CORRECTION** | True; equality needs `w \| m` (at `m=65`, `w=64` the ratio is off by 97 %) |
| — | crossover `s* = c_w/(c_p·w·V)` | **CONFIRMED** | Exact solution of the stated constant-factor model; honestly labelled heuristic |
| P6 | `E|A∩B| = ab/m`; hypergeometric variance | **CONFIRMED** | Mean and variance verified exactly (rational arithmetic) for all `m ≤ 25`, all `(a,b)` |
| P7 | `e^{−ab/(m−a−b+1)} ≤ Pr[∅] ≤ e^{−ab/m}` | **CONFIRMED WITH CORRECTION** | Upper bound holds for **all** `(m,a,b)`; **lower bound is false without the unstated hypothesis `a+b ≤ m`** — 57 155 violations found for `m ≤ 69` |
| P8 `B`,`S` | `⌈m/w⌉`, `md` | **CONFIRMED** | Definitional; both exact, not "leading order" |
| P8 `R` | `d+(m−1)d(1−d)`; fixed-card `k(m−k+1)/m` | **CONFIRMED** | Both verified exactly — Bernoulli exhaustively for `m ∈ {1,2,5,9,12}`, fixed-cardinality exhaustively for all `m ≤ 14`, all `k`. They are genuinely different models: `= m d(1−d) + d²` vs `+ d` |
| P8 `W` | `≈ (m/w)[1−(1−d)^{2w}−d^{2w}]` | **CONFIRMED WITH CORRECTION** | The `2w` exponent is right and the fill-group counting is right. Simulated against a real WAH/EWAH encoder at `w ∈ {8,64}`, `d ∈ [10⁻³,0.999]`: agreement ≤ 0.5 % (≤ 2.8 % at `n·p₀` ≈ 300). The **exact** Bernoulli value is `n − (n−1)(p₀²+p₁²)`, which I verified exhaustively at `m=12, w=3`; the document's form overstates it by `p₀²+p₁² ≤ 2` words |
| P9 `S` | `κ_S(Xᶜ) = m − κ_S(X)` | **CONFIRMED** | Definitional |
| P9 `R` | `|r(Xᶜ) − r(X)| ≤ 1` | **CONFIRMED, and sharpenable** | Exhaustive for `m ≤ 16`. The exact rule is `r(Xᶜ) = r(X) − 1 + [0∉X] + [m−1∉X]`; the sign of the ±1 is determined entirely by boundary membership (§3.6) |
| P9 `W` | `κ_W(Xᶜ) = κ_W(X)` exactly | **CONFIRMED WITH CORRECTION** | Exhaustively true for `(m,w) ∈ {(16,4),(16,8),(12,4),(8,2)}`; **fails when `w ∤ m`** (counterexamples at `(10,4)`, `(13,4)`, `(9,8)`) because the padded tail word is never all-ones |
| Cor A | monotonicity in `s`; U-shape under the null | **CONFIRMED WITH CORRECTION** | Monotonicity holds for the leading-order forms (verified). Two defects: the sandwich's upper half `E[r] ≤ m·s` is violated by up to `+1` at the extreme dense corner (38 violations found), and **"selection would buy at most a constant factor" directly contradicts Theorem A** (§2.1) |
| **Thm B** | `κ_R`, `κ_W` vary by `Θ(m)` at fixed density | **CONFIRMED — proof defective** | The statement is exactly right: `κ_R` attains **every** integer in `[1, min(k, m−k+1)]`, verified exhaustively for all `m ≤ 14`, all `k`. But **`X_spread` = "every `⌈1/d⌉`-th position" does not have density `d`** unless `1/d` is an integer (at `d=0.3`, `m=10⁶` it has density 0.25) — §2.2 gives the repair |
| P10 | `σ_R ≥ 1−d+1/m ≥ ½`, unbounded above | **CONFIRMED, and weaker than the truth** | Verified exhaustively for `m ≤ 14`. The general bound, valid at all densities, is `σ_R ≥ max(d, 1−d+1/m)`; the stated form degenerates to `0.002` at `d=0.999` where the truth is `0.999` (§3.3) |
| Cor B | density-based selection cannot see `σ` | **CONFIRMED as an impossibility; two consequences OVERCLAIMED** | The impossibility is exact and trivially so. "Modelled selection can be worse than no selection at all … a derived prediction" and "that is probe-and-commit" do **not** follow (§2.3, §2.4) |
| P11 | binned refinement never widens the interval | **CONFIRMED; one rider REFUTED** | Containment and the width statement verified on 3 000 random instances. The rider "`popcount(occ_A ∧ occ_B)` is **exactly** the number of bins whose refined interval is non-degenerate" is **false** — a bin where both sides are full is occupied but degenerate; `≥` is the correct relation (§3.5) |
| P12 | headroom vanishes at `s = ½` | **CONFIRMED** | All four arithmetic claims check (`w/2 = 32`, `E[r]/κ_B = 16`, `E[κ_W] → m/w`). "It is **negative**" is wrong wording for a ratio of `1/32` |
| §9.1 | "in the pure membership-probe model the true bound is `Ω(m − max(a,b))`" | **UNVERIFIABLE / OVERCLAIMED** | Unproved and unsourced, and not tight: at `a=b=m−1` it asserts `Ω(1)` where an adversary forces `Ω(m)` probes (§2.5) |
| §10 | worked examples A–D | **CONFIRMED** | Every number reproduced. One typo: `Pr[∅]` at `m=1000, a=b=50` is `0.07198`, i.e. `0.0720`, not `0.0721` |
| §13 | figure spec closed forms | **CONFIRMED** | `kappa_*(d)` in the spec are `E[κ]/m` and are consistent with §5. One caption sentence is wrong (§2.1) |

---

## 2. Wrong and load-bearing

### 2.1 Corollary A contradicts Theorem A — "selection would buy at most a constant factor"

**As written (§5, Corollary A):**

> "Consequently, under the null model the whole matrix collapses: every cell's expected cost is
> `Θ(min(m/w, m·s))` and **selection would buy at most a constant factor**."

and, propagated into the Fig. 2b caption (§13.3):

> "no representation beats another by more than a constant"

Both are false, and they are false *because of Theorem A*. `B` is a member of the matrix, its expected
cost is `m/w` at every density, and the cheapest of the others is `≈ m·s`. The ratio is `1/(w·s)`,
which is the document's own headline and is unbounded. Measured on the document's own closed forms:

| `s` | `E[κ_B]/m` | `E[κ_S∨C∘S]/m` | `E[κ_R]/m` | `E[κ_W]/m` | max/min over the matrix |
|---|---|---|---|---|---|
| `10⁻⁶` | 1.5625e−2 | 1.0e−6 | 1.0e−6 | 2.0e−6 | **15 625×** |
| `10⁻⁴` | 1.5625e−2 | 1.0e−4 | 1.0e−4 | 2.0e−4 | 156× |
| `10⁻²` | 1.5625e−2 | 1.0e−2 | 9.9e−3 | 1.13e−2 | 1.6× |
| `0.5` | 1.5625e−2 | 0.5 | 0.25 | 1.5625e−2 | 32× |

It is also false that "every cell's expected cost is `Θ(min(m/w, m·s))`" — that is true of the
*envelope*, not of every cell; `κ_B` is `m/w` regardless.

**Corrected statement.**

> Under the null model, the four **compressed** representations agree to within a factor of two:
> `½·m·s ≤ E[κ_R] ≤ m·s + 1`, `E[κ_S∨C∘S] = m·s`, and `E[κ_W] → 2·m·s` as `s → 0` while saturating
> at `m/w`. Selection *among* `{S, C∘S, R, W}` therefore buys at most a constant under randomness.
> Selection *between the bitmap and the rest* is exactly Theorem A and buys an unbounded factor —
> which is the point. The envelope of the matrix is `Θ(min(m/w, m·s))`, verified to lie within
> `[½, 1]×min(1/w, s)` at every `s ∈ (0, ½]`.

The sparse-limit constant is worth stating because the figure spec depends on it:
`(1/w)[1−(1−s)^{2w}] ~ 2s`, so **`W` costs about twice `S` in the sparse tail**, not the same.
`theory_block.tex` (l. 66–68) escapes this error because it scopes the sentence to `R` and `S`; only
`THEORY.md` and the Fig. 2b caption carry it. **Severity: wrong and load-bearing** — a referee reading
Corollary A immediately after Theorem A will notice.

### 2.2 Theorem B's `X_spread` family does not have density `d`

**As written (§6, proof of Theorem B):** "Take `X_spread` = every `⌈1/d⌉`-th position: every element
isolated, so `r = k`."

That set has cardinality `⌈m/⌈1/d⌉⌉`, which equals `k = dm` **only when `1/d` is an integer**. The
theorem is a statement about sets *of density exactly `d`*, so the family must hit `k` exactly.
Measured at `m = 10⁶`:

| `d` | `k = dm` | `⌈1/d⌉` | `\|X_doc\|` | actual density |
|---|---|---|---|---|
| 0.30 | 300 000 | 4 | 250 000 | **0.250** |
| 0.45 | 450 000 | 3 | 333 334 | **0.333** |
| 0.07 | 70 000 | 15 | 66 667 | **0.0667** |
| 1/3 | 333 333 | 3 | 333 334 | 0.333334 |
| 0.20 | 200 000 | 5 | 200 000 | 0.200 ✔ |

**Corrected family.** `X_spread := { i·⌊m/k⌋ : 0 ≤ i < k }`.

- `|X_spread| = k` exactly (the last element is `(k−1)⌊m/k⌋ < m`).
- Gap `⌊m/k⌋ ≥ 2` whenever `k ≤ m/2`, so every element is isolated and `r = k`.
- Every `w`-bit word contains at least one element iff `⌊m/k⌋ ≤ w`, i.e. iff `d ≥ 1/w` — which is
  exactly the case split the document already uses. At `m=10⁶`, `w=64` this gives
  `κ_W = m/w = 15 625` for `d ∈ {1/64, 0.1, 0.5}` and `κ_W = 2k` for `d ∈ {10⁻⁴, 10⁻³}`, both as the
  theorem asserts.

**The theorem itself is correct** and I verified the strong form exhaustively: over all `k`-subsets of
`[0,m)` for every `m ≤ 14`, the set of attainable run counts is *exactly* `{1, …, min(k, m−k+1)}` —
no gaps, both endpoints attained. The "merge adjacent isolated elements one at a time" argument for
intermediate values is sound but is more cleanly replaced by: *choose any composition of `k` into `r`
parts and any distribution of the `m−k` zeros into the `r+1` gaps with the `r−1` internal gaps
non-empty; this is feasible iff `r ≤ k` and `r ≤ m−k+1`.*

`theory_block.tex` inherits the same defective family ("every `⌈1/d⌉`-th position, `k` of them",
l. 74–75). Its range `[1,k]` is fine, because it carries the hypothesis `d ≤ ½` under which
`min(k, m−k+1) = k`.

**Severity: the proof is wrong, the theorem is right.** Load-bearing as a *proof*, since Theorem B is
the document's main structural result and this is its only construction.

### 2.3 "Modelled selection can be worse than no selection at all … a derived prediction"

**As written (§6, Corollary B point 2):**

> "A cost model evaluated from cardinality metadata … reproduces `E_null(d)[κ]` **by construction** and
> is therefore wrong by a factor of `σ` on exactly the rows where the largest win is available. This
> is why modelled selection can be **worse than no selection at all** (claim A6), and it is a
> *derived* prediction, not an empirical accident."

Three separate problems.

1. A density-only cost model need not equal `E_null(d)[κ]`. It is *some* function `f(d)`; the
   document assumes the worst-case one and calls the assumption "by construction".
2. "Wrong by a factor of `σ`" does not imply "worse than no selection". Regret depends on which cell
   the wrong prediction selects and on that cell's actual cost, neither of which is modelled here.
   A6 (per-pair model 403.7 % regret vs all-bitmap 118.0 %) is a **measurement**, and it is a good
   one; the theory does not derive it and should not claim to.
3. `σ_ρ` is defined against the *null expectation*, so "wrong by a factor of `σ`" is a tautology of
   the definition, not a consequence.

**Corrected statement.**

> Any selector whose input is `d` (or `a`, `b`, or any function of them) evaluates the same fixed
> function `f(d)` on every row of that density. Since `κ_R` takes **every** value in `[1, k]` at
> density `d = k/m`, `f(d)` is wrong by an unbounded factor on some rows at every density, in at
> least one direction. Whether that mis-prediction costs more than not selecting at all depends on
> the cell it selects and is measured (A6), not derived.

**Severity: wrong and load-bearing** — it is the theory's stated justification for the paper's
central design decision, and it is the sentence a referee will test first.

### 2.4 "Selection must consult a statistic that sees structure … that is probe-and-commit"

**As written (§6, Cor B point 3 + the sentence after):** "Point 3 is the strongest thing in this
document: the paper's central design decision follows from the theory rather than from a measurement."
And in `theory_block.tex` (l. 88–89): "Selection must consult a statistic that sees structure — **the
argument for measuring candidate kernels rather than modelling them**."

The proved statement is: *a density-only statistic cannot distinguish `X_block` from `X_spread`.*
That rules out one class of selector. It does **not** select among the survivors, and the document's
own point 3 lists a survivor that is not probe-and-commit: a run count, or a zone-map occupancy count,
is `O(1)` metadata that sees `σ` perfectly and can be fed to a *model*. So the theory licenses
"consult a structural statistic", and is silent between "model on a structural statistic" and
"measure the kernels". The `theory_block.tex` sentence turns a proved dichotomy into an unproved
trichotomy resolution, and it does so in the LaTeX that goes into Methods.

**Corrected statement.** Replace the em-dash clause with: "— the argument for gating selection on a
statistic that sees structure rather than on cardinality. Whether that statistic is a cheap structural
summary or a direct measurement of the candidate kernels is settled by measurement, not here."

**Severity: wrong and load-bearing** (it is the one sentence the document nominates as "the strongest
thing in this document").

### 2.5 §9.1's own lower-bound claim is unsourced and not tight

The guard-rail section correctly forbids lower-bound-on-work language, and then makes one:

> "In the pure membership-probe model the true bound is `Ω(m − max(a,b))` — much *stronger* than
> `W_unc`."

Counterexample to tightness: take `a = b = m−1`. Then `m − max(a,b) = 1`, so the claimed bound is
`Ω(1)`. But `|A ∩ B|` is `m−1` iff `A` and `B` omit the same position, and an adversary answering
membership queries can force `m−1` probes before the omitted position of `A` is located. The true
worst case there is `Ω(m)`. So `Ω(m−max(a,b))` is at best *a* valid lower bound, not "the true bound",
and no proof or citation is given for it either way. Under the project's own evidence ladder this is a
tier-1 recalled claim in a section whose purpose is to forbid exactly that.

**Corrected statement.** Either drop the sentence, or: "Membership-probe lower bounds for this problem
are stronger than `W_unc` — for instance no algorithm restricted to membership probes can beat `Ω(m)`
when `a = b = m−1` — but the matching bound is a comparison-complexity question with its own
literature and must be sourced before it is cited."

**Severity: wrong but confined to a guard-rail paragraph** — however it is the *only* lower-bound
assertion in the document and it is in the section that forbids them.

### 2.6 "What the problem *requires*" — the retracted claim is still in the framing

§9.1 says: "Nothing above proves any algorithm must perform `Ω(m·s)` operations. … Do not write
'must'." But the framing does:

- `THEORY.md` §0, purpose item 3: "the gap between **what the problem requires** and what the standard
  method spends is provable".
- `theory_block.tex` l. 3–4: "a gap must exist between **what an intersection cardinality *requires***
  and what a dense-bitmap kernel *spends*".

"Requires" is a lower bound on work. The proved object is a gap between **two achievable costs**. This
is the same overclaim the document retracted as "information-theoretic floor", surviving in the
sentence a reader meets first — and in the LaTeX destined for Methods.

**Corrected statement (LaTeX).** "…a gap must exist between what a dense-bitmap kernel spends and what
a cheaper but equally exact algorithm on the same operands already achieves, and representations whose
cost is a function of structure rather than of the universe can enter it."

`NARRATIVE.md` §1c carries three more instances that the errata list does not cover: "The same
quantity bounds how much work is **unavoidable**"; "in the generic case the work is **irreducible** and
**no selection strategy can avoid it**"; "how closely real kernels approach **the floor**". The
guard-rail box in §1c renames the phrase but leaves the surrounding sentences making the claim.

**Severity: wrong and load-bearing**, precisely because the document knows it and fixed it in one
place only.

---

## 3. Correct but stated imprecisely

### 3.1 Proposition 5's hypotheses are over- and under-specified

- **Over:** the `O(1)` rank index is never used. All four cases need only membership probes and the
  cardinalities `a`, `b`. (Rank *is* needed for the `B×R` two-lookup example of §6, where it is not
  stated as a hypothesis.)
- **Under:** "a sorted array of its set positions, **and likewise for its complement**" is `Θ(m)`
  space when read literally. The honest hypothesis: each operand is available as a bitmap plus a
  sorted array of **whichever of `X`, `Xᶜ` is smaller** — that is `Θ(m·s_X)` space, and which one to
  build is decidable from `a`, `b`, `m` in `O(1)`.
- One further consequence worth stating: without the materialised complement array, enumerating `Xᶜ`
  from the bitmap costs `Θ(m/w + (m−a))`, whose first term is exactly `κ_B`. The dense arm of the
  argument therefore genuinely requires the complement to be *stored*, not derived on the fly.

Verified: the four-case algorithm returns the exact answer and uses exactly `W_unc` probes on 4 000
random instances with `m ≤ 60`.

### 3.2 Theorem A needs `w | m`

`⌈m/w⌉/(m·s) = 1/(w·s)` holds only when `w` divides `m`. Relative error at `m=1000, w=64` is 2.4 %; at
`m=100` it is 28 %; at `m=65` it is 97 %. Either state `w | m` (true for the implementation) or write
`≥` with an additive `w/m` term. Nothing downstream changes.

### 3.3 Proposition 10 proves less than is true, and states its consequence unrestrictedly

Stated: `σ_R ≥ (m−k+1)/m = 1−d+1/m ≥ ½`, under `d ≤ ½`. The proof's own step generalises:

```
σ_R ≥ E[r]/r_max = [k(m−k+1)/m] / min(k, m−k+1) = max(k, m−k+1)/m = max(d, 1−d+1/m) ≥ ½
```

valid at **every** density. This matters because the read-out-loud sentence — "Real data can be
arbitrarily cheaper than random data at the same density, and **at most about twice as expensive**" —
is stated without a density restriction while only the `d ≤ ½` branch is proved. At `d = 0.999` the
stated bound gives `σ_R ≥ 0.002` (a 500× worst case), whereas the truth is `σ_R ≥ 0.999`. The
unrestricted sentence is *true*; use the general bound so that it is also *proved*. Verified
exhaustively for `m ≤ 14`, all `k`, all subsets. `theory_block.tex` l. 80 ("bounded below by `1−d`")
inherits the weak form.

### 3.4 Which null model is `σ` defined against?

§5 declares the section's model to be Bernoulli; P10 and the §13.2 schema (`sigma_R = k(m−k+1)/m ÷
n_runs`) use the **fixed-cardinality** expectation. The two differ by `d − d² = O(1)`, so nothing
numerically material follows, but `σ` is a headline reported quantity (a new Table 1 column) and its
denominator convention must be pinned in one place. Recommend: define `σ` against the
fixed-cardinality null throughout, since that is the model that conditions on the observable (`k`) and
it is exact.

### 3.5 The zone-map rider in Proposition 11 is an over-equality

> "the popcount of `occ_A ∧ occ_B` is the number of bins whose refined interval is non-degenerate,
> which is exactly the number of bins the pairing must visit"

The second clause is right; the first is false. A bin in which **both** sides are full is occupied on
both sides, so it is counted by the popcount, but its refined interval is the single point `β` —
degenerate. Found immediately in random testing. Correct relation:

```
#{bins with a non-degenerate refined interval}  ≤  popcount(occ_A ∧ occ_B)
```

with equality iff no bin is saturated on both sides. The operationally useful claim — *popcount zero
proves disjointness, and popcount bounds the bins that must be visited* — is untouched.
`NARRATIVE.md`'s filter table ("`occ_A ∧ occ_B` popcount is the exact bin count to visit") is fine as
written; only the `THEORY.md` gloss overreaches.

### 3.6 Proposition 9's ±1 has an exact form; state it

Verified exhaustively for `m ≤ 16` (all `2^m` sets):

```
r(Xᶜ) = r(X) − 1 + [0 ∉ X] + [m−1 ∉ X]
```

so the sign of the ±1 is fixed entirely by boundary membership:

| `0 ∈ X` | `m−1 ∈ X` | `r(Xᶜ) − r(X)` |
|---|---|---|
| no | no | `+1` |
| no | yes | `0` |
| yes | no | `0` |
| yes | yes | `−1` |

Degenerate cases obey it: `X = ∅` gives `r=0, r(Xᶜ)=1`; `X = [0,m)` gives `r=1, r(Xᶜ)=0`. This answers
the brief's question directly: **a set touching position 0 or `m−1` does change the count**, and the
`±1` is never a two-sided uncertainty for a *given* set — it is determined.

Correspondingly, `κ_W(Xᶜ) = κ_W(X)` is exact **only when `w | m`**. With a zero-padded tail word, a
word that is all-ones over the live bits is not all-ones over the padded word, so complementation
moves it between the literal and fill classes. Counterexamples found at `(m,w) = (10,4), (13,4),
(9,8)`. Add the side condition, alongside the existing (correct) both-fills-compressed one.

### 3.7 Corollary A's monotonicity is a statement about the leading-order forms

The exact Bernoulli `E[r] = d + (m−1)d(1−d)` is **not a function of `s`**: at `m = 1000`,
`E[r](0.1) = 90.01` but `E[r](0.9) = 90.81`, a gap of `1−2d`. Its maximum is at
`d = ½ + 1/(2(m−1))`, not at `d = ½`, so it is not even monotone in `s` on the last `1/m` of the dense
branch. Everything is `O(1)` and nothing downstream cares, but Corollary A says "Every entry of
Proposition 8 is, as a function of `s`, …" and the `R` entry in Proposition 8's table is the exact
form. Say "the leading-order forms". By contrast `E[κ_W]` **is** exactly a function of `s`
(`(1−d)^{2w} + d^{2w}` is symmetric), and `E[κ_R]`'s leading term `m·s(1−s)` is too.

### 3.8 The Corollary A sandwich is off by one at the dense corner

`½·m·s ≤ E[κ(R)] ≤ m·s` holds exactly for the leading-order form `m·s(1−s)` (verified for all
`s ∈ [0,½]`), but is **violated by the exact expectations** near `d → 1`. 38 violations found; e.g.
`m=64, k=57`: fixed-card `E[r] = 7.125 > m·s = 7`. At `m=1000, k=999`: `E[r] = 1.998` against
`m·s = 1`, a ratio of 2.

**Corrected statement.** `½·m·s ≤ E[κ(R)] ≤ m·s + 1`, since fixed-card `E[r] = m·s(1−s) + d` and
Bernoulli `E[r] = m·s(1−s) + d²`. The worst ratio `E[r]/(m·s)` is `d(1 + 1/(m−k)) → 2`. The "within a
factor of two of `S`" reading survives unchanged. **Severity: cosmetic**, but `theory_block.tex`
states it as an inequality chain in display maths, where it will be checked.

### 3.9 `E[κ_W]` in the sparse limit is `2ms`, not `ms`

`(1/w)[1 − (1−s)^{2w} − s^{2w}] → 2s` as `s → 0`. So under the null model a fill-compressed stream
costs **twice** a sorted array in the sparse tail — verified numerically (at `s=10⁻⁶`, `E[κ_W]/m =
1.9999×10⁻⁶`). Neither §5 nor the Fig. 2b caption says this, and the caption's "a fill-compressed
stream saturates onto the bitmap line" describes only the dense half of its behaviour. Worth one
clause, because it is the reason `W` never appears on the envelope.

### 3.10 Smaller items

| Item | Where | Fix |
|---|---|---|
| `Pr[∅]` at `m=1000, a=b=50` is `0.07198` | §4, P7 | `0.0720`, not `0.0721`. The "exponent ratio 1.052" is exact-vs-**upper**; the ratio of the two *bounds* is 1.110. Say which |
| "the headroom … is **negative**" | §8, P12 | The ratio is `1/32`. Say "falls below one" (as `theory_block.tex` correctly does) |
| `W_unc` vs `W` | throughout | The interval width and the WAH representation share a letter. Rename the width `U(a,b)` or `Δ(a,b)` |
| "every **cell**'s expected cost" | §5, Cor A | These are per-*operand* descriptor lengths, not per-*cell* costs; a cell's cost is a function of both operands' representations |
| "No cost law is claimed for `R` or `W`. **Their costs are `r` and `ℓ+g`**" | §9.4 | The second sentence asserts the cost law the first denies. Say: "their descriptor lengths are `r` and `ℓ+g`; no map from descriptor length to time is claimed" |
| `E[κ_S] = md` labelled "to leading order in `m`" | `theory_block.tex` l. 53–55 | `κ_B` and `κ_S` are exact; only `κ_R` and `κ_W` are leading-order |
| §5 header promises exact fixed-cardinality forms "where they differ" | §5 | Given for `R`, not for `W` (word occupancies are not independent under a fixed-cardinality draw). Either supply it or narrow the promise |

---

## 4. Does the theory support the thesis?

The thesis chain is: *bitmaps have a fixed cost → equivalent representations deviate from it → we
exploit that deviation to avoid work.*

**Links 1 and 2 are proved.** P4 gives the fixed cost. Theorem B gives deviation, and gives it in the
strongest available form: at fixed density, `κ_R` attains *every* integer in `[1,k]`. P10 gives the
one-sidedness that makes the deviation exploitable rather than merely present. Cor B gives the
impossibility that motivates a structural selector. That is a genuine and non-obvious chain.

**Link 3 is not proved, and the document reads as though it were.** Theorem B is about **descriptor
length**. The thesis needs **cell cost**. The only place the document crosses that bridge is the
worked case C — "`B×R` with a rank index answers the query in two rank lookups" — which is an
unlabelled example, not a numbered result. There is no proposition anywhere of the form:

> **Missing proposition.** Given `A` as a bitmap with an `O(1)` rank index and `B` as a run container
> with runs `{[u_i, v_i)}`, `|A ∩ B| = Σ_i (rank_A(v_i) − rank_A(u_i))`, so the `B×R` cell costs
> `2·r(B)` rank lookups — **independent of `m`, of `a`, and of the run lengths**. Symmetrically,
> `R×R` by merge costs `O(r_A + r_B)`.

That statement is elementary, exactly true, and it is the *only* thing that converts Theorem B from a
fact about storage into a fact about work. Its absence is why §9.4's guard rail comes out
self-contradictory (§3.10). The paper currently carries this link as measurement A7 ("a rank index
makes `B×R` cost `Θ(runs)`, independent of run length"); it should be derived and then confirmed, not
only measured. **Recommend adding it as Proposition 5b.** It also retroactively justifies the rank
index in P5's hypotheses, which is currently unused there.

**The U-shape discipline holds.** §9.3, Corollary A's closing paragraph, errata item 1 and
`theory_block.tex` l. 64–66 all state the U-shape as a property of the null model and of the bitmap's
flatness, and explicitly deny that the method reproduces it. Theorem B is the correct instrument for
that denial. There is **no** place in `THEORY.md` or `theory_block.tex` that claims the U holds for
non-bitmap representations on real data. **However**, `NARRATIVE.md`'s claim ledger still carries:

> **A2** — "Cost is monotone in **effective sparsity** `s = min(d,1−d)`, hence U-shaped and symmetric
> about `d = ½` **by construction**"

as a claim about *measured* cost. `THEORY.md` errata item 1 flags this and the narrative has not been
updated. That is the one live inconsistency between the theory and the paper's thesis; Theorem B is a
counterexample to A2 as worded. A2 should be restated as: *`B×B`'s cost is flat, and the envelope of
the compressed cells is monotone in `s` under the null; what is measured is the crossover in `s`, the
magnitude, and the deviation below the null.*

---

## 5. Verification scripts

Both are stdlib-only and are the exact scripts whose output is quoted above.

### 5.1 `verify.py`

```python
#!/usr/bin/env python3
"""Adversarial numerical verification of paper/THEORY.md.
stdlib only: itertools, random, math, fractions."""

import itertools, random, math
from fractions import Fraction as F
from math import comb, exp, log, ceil

random.seed(20260805)
FAILS = []
def check(name, ok, detail=""):
    print(("PASS " if ok else "FAIL ") + name + (("  | " + detail) if detail else ""))
    if not ok: FAILS.append(name)

# ---------------------------------------------------------------- P1 / P2
print("\n=== P1 Frechet interval: attainment of every integer ===")
ok = True
for m in range(0, 9):
    for a in range(0, m+1):
        for b in range(0, m+1):
            lo, hi = max(0, a+b-m), min(a, b)
            attained = set()
            for A in itertools.combinations(range(m), a):
                sA = set(A)
                for B in itertools.combinations(range(m), b):
                    attained.add(len(sA & set(B)))
            if attained != set(range(lo, hi+1)): ok = False; print("  mismatch", m,a,b,sorted(attained),lo,hi)
            for t in range(lo, hi+1):                      # the proof's explicit construction
                A = set(range(0, a)); B = set(range(a-t, a-t+b))
                if not (B <= set(range(m))) or len(A & B) != t:
                    ok = False; print("  construction fails", m,a,b,t)
check("P1 exhaustive attainment + proof construction (m<=8)", ok)

print("\n=== P2 width identity ===")
ok = True
for m in range(0, 200):
    for a in range(0, m+1):
        for b in range(0, m+1):
            w1 = min(a,b) - max(0, a+b-m)
            w2 = min(a, b, m-a, m-b)
            sA, sB = min(F(a,m) if m else 0, 1-F(a,m) if m else 0), (min(F(b,m), 1-F(b,m)) if m else 0)
            w3 = m*min(sA, sB) if m else 0
            if not (w1 == w2 == w3): ok = False; print("  ", m,a,b,w1,w2,w3)
check("P2 min(a,b)-max(0,a+b-m) = min(a,b,m-a,m-b) = m*min(s_A,s_B), exhaustive m<=199", ok)

print("\n=== P3 pigeonhole floor, factor 2 ===")
m = 10**6; d = 0.51
check("P3 floor at d=.51 is 0.02m (factor 2, not 1)", abs((2*d-1)*m - 0.02*m) < 1e-6,
      f"floor={(2*d-1)*m:.0f}; as fraction of operand 2-1/d={2-1/d:.4f}")
ok = all(((max(0,a+b-m0) > 0) == (a+b > m0)) for m0 in range(1,60) for a in range(m0+1) for b in range(m0+1))
check("P3 floor lifts off exactly when d_A+d_B>1", ok)
ok = all(abs((d0*m - (2*d0-1)*m) - m*(1-d0)) < 1e-6 for d0 in [0.5,0.6,0.75,0.9,0.99])
check("P3/errata-3 width above d=1/2 equals m(1-d); d/dd = -m (rate, not width)", ok)

# ---------------------------------------------------------------- P5 achievability
print("\n=== P5 achievability: four-case probe algorithm, cost == W_unc ===")
def four_case(m, A, B):
    a, b = len(A), len(B)
    Wu = min(a, b, m-a, m-b)
    Ac = set(range(m)) - A; Bc = set(range(m)) - B
    if Wu == a:      return sum(1 for i in A  if i in B), a
    elif Wu == b:    return sum(1 for i in B  if i in A), b
    elif Wu == m-a:  return b - sum(1 for i in Ac if i in B), m-a
    else:            return a - sum(1 for i in Bc if i in A), m-b
ok = True
for trial in range(4000):
    m = random.randint(1, 60)
    A = set(random.sample(range(m), random.randint(0,m))); B = set(random.sample(range(m), random.randint(0,m)))
    val, probes = four_case(m, A, B)
    if val != len(A & B) or probes != min(len(A),len(B),m-len(A),m-len(B)): ok = False
check("P5 all four cases exact, probe count == W_unc", ok)

# ---------------------------------------------------------------- Theorem A
print("\n=== Theorem A ratio ===")
for (m,w,s) in [(10**6,64,1e-4),(10**6,64,0.5),(10**6,64,0.01)]:
    print(f"  m={m} w={w} s={s}: ceil(m/w)/(m s)={ceil(m/w)/(m*s):.4f}  1/(w s)={1/(w*s):.4f}")
m,w,s = 1000, 64, 0.01
print(f"  w does NOT divide m: m={m}: {ceil(m/w)/(m*s):.5f} vs {1/(w*s):.5f}")
check("Thm A: 1/(ws) exceeds 1 iff s<1/w", all(((1/(w*s0) > 1) == (s0 < 1/w)) for s0 in [1e-5,0.01,1/64,0.02,0.3,0.5]))
c_w, c_p, V, w = 3.7, 11.0, 8, 64
s_star = c_w/(c_p*w*V)
check("crossover s* = c_w/(c_p w V) solves (m/w)(c_w/V) = m s c_p",
      abs((10**6/w)*(c_w/V) - 10**6*s_star*c_p) < 1e-6, f"s*={s_star:.3e}")

# ---------------------------------------------------------------- P6
print("\n=== P6 hypergeometric mean and variance ===")
def hyper_moments(m,a,b):
    tot = comb(m,b); e = F(0); e2 = F(0)
    for t in range(max(0,a+b-m), min(a,b)+1):
        p = F(comb(a,t)*comb(m-a,b-t), tot); e += p*t; e2 += p*t*t
    return e, e2 - e*e
ok = True
for m in range(2, 26):
    for a in range(0,m+1):
        for b in range(0,m+1):
            e, v = hyper_moments(m,a,b)
            if e != F(a*b, m): ok = False
            if v != F(b,1)*F(a,m)*F(m-a,m)*F(m-b,m-1): ok = False
check("P6 E=ab/m and Var=b(a/m)((m-a)/m)((m-b)/(m-1)) exactly, m<=25", ok)

# ---------------------------------------------------------------- P7
print("\n=== P7 disjointness ===")
def pr_disj(m,a,b):
    if a+b > m: return F(0)
    return F(comb(m-a,b), comb(m,b))
ok_prod = ok_up = ok_lo = True; viol = []
for m in range(1, 70):
    for a in range(0, m+1):
        for b in range(0, m+1):
            p = pr_disj(m,a,b)
            prod = F(1)
            for j in range(b): prod *= (1 - F(a, m-j)) if m-j>0 else F(0)
            if p != prod: ok_prod = False
            if float(p) > exp(-a*b/m) + 1e-12: ok_up = False
            den = m-a-b+1
            if den > 0:
                if float(p) < exp(-a*b/den) - 1e-12: ok_lo = False
            else:
                try: lb = exp(-a*b/den) if den != 0 else float('inf')
                except OverflowError: lb = float('inf')
                if lb > float(p) + 1e-12: viol.append((m,a,b))
check("P7 product form == C(m-a,b)/C(m,b)", ok_prod)
check("P7 upper bound e^{-ab/m} holds for ALL (m,a,b)", ok_up)
check("P7 lower bound holds whenever a+b<=m", ok_lo)
check("P7 lower bound FAILS when a+b>m (unstated hypothesis)", len(viol) > 0, f"{len(viol)} violations")
m,a,b = 1000,50,50
p = float(pr_disj(m,a,b))
print(f"  m=1000,a=b=50: exact={p:.5f} (doc says 0.0721); upper={exp(-2.5):.4f}; lower={exp(-a*b/(m-a-b+1)):.4f}")
print(f"  -ln(exact)/(ab/m) = {-log(p)/(a*b/m):.4f}; predicted 1+(a+b)/2m = {1+(a+b)/(2*m):.4f}")
print(f"  ratio of the two BOUND exponents = {(a*b/(m-a-b+1))/(a*b/m):.4f}")
for d0 in [1e-3, 1e-2, 5e-2, 0.1]:
    m0 = 10000; a0 = b0 = int(d0*m0)
    p0 = float(pr_disj(m0,a0,b0)); u = exp(-a0*b0/m0); l = exp(-a0*b0/(m0-a0-b0+1))
    print(f"    d={d0:<6} exact={p0:.3e} upper/exact={u/p0 if p0 else float('inf'):.4f} exact/lower={(p0/l) if l else 0:.4f}")
check("P7 corollary: two singletons meet w.p. 1/m", all(pr_disj(m0,1,1) == F(m0-1,m0) for m0 in range(1,200)))

# ---------------------------------------------------------------- runs
print("\n=== P8 E[#runs]: two models ===")
def nruns(bits):
    r = 0; prev = 0
    for x in bits:
        if x and not prev: r += 1
        prev = x
    return r
ok_fix = ok_ber = True
for m in range(1, 15):
    for k in range(0, m+1):
        s = 0
        for S in itertools.combinations(range(m), k):
            bits = [0]*m
            for i in S: bits[i] = 1
            s += nruns(bits)
        if F(s, comb(m,k)) != F(k*(m-k+1), m): ok_fix = False
check("P8 fixed-cardinality E[r] = k(m-k+1)/m exactly, m<=14 all k", ok_fix)
for m in [1,2,5,9,12]:
    for d0 in [F(1,4), F(1,3), F(1,2), F(3,4)]:
        e = F(0)
        for bits in itertools.product([0,1], repeat=m):
            k = sum(bits); e += d0**k * (1-d0)**(m-k) * nruns(bits)
        if e != d0 + (m-1)*d0*(1-d0): ok_ber = False
check("P8 Bernoulli E[r] = d + (m-1)d(1-d) exactly", ok_ber)
print("  models DIFFER: Bernoulli = m d(1-d) + d^2 ; fixed-card = m d(1-d) + d")

print("\n=== Corollary A sandwich  1/2 m s <= E[r] <= m s ===")
bad_up = []; bad_lo = []
for m0 in [64, 1000, 10**6]:
    for k0 in list(range(0, min(m0,64))) + [m0//4, m0//2, m0-3, m0-2, m0-1, m0]:
        if k0 > m0: continue
        d0 = k0/m0; s0 = min(d0, 1-d0)
        for name, e in (("fixed", k0*(m0-k0+1)/m0), ("bern", d0 + (m0-1)*d0*(1-d0))):
            if e > m0*s0 + 1e-9: bad_up.append((name, m0, k0, e, m0*s0))
            if e < 0.5*m0*s0 - 1e-9: bad_lo.append((name, m0, k0))
check("Corollary A lower half  E[r] >= m s / 2", not bad_lo)
check("Corollary A upper half  E[r] <= m s  (EXACT expectations)", not bad_up,
      f"{len(bad_up)} violations, e.g. {bad_up[:4]}")
print("  corrected: 1/2 m s <= E[r] <= m s + 1")
print(f"  worst fixed-card E[r]/(m s) over m=1000: "
      f"{max((k0*(1000-k0+1)/1000)/(1000*min(k0/1000,1-k0/1000)) for k0 in range(1,1000)):.4f}")

# ---------------------------------------------------------------- EWAH
print("\n=== P8 W: literal words + fill groups, formula vs real encoder ===")
def wah_counts(bits, m, w):
    """(literals, fill groups, total EWAH words). w-bit words, zero-padded tail."""
    n = (m + w - 1)//w
    kinds = []
    for j in range(n):
        chunk = bits[j*w:(j+1)*w]; chunk = chunk + [0]*(w - len(chunk)); s = sum(chunk)
        kinds.append(0 if s == 0 else (1 if s == w else 2))   # 0-fill, 1-fill, literal
    lit = sum(1 for x in kinds if x == 2)
    g = sum(1 for j, x in enumerate(kinds) if x != 2 and (j == 0 or kinds[j-1] != x))
    markers = g + (1 if kinds and kinds[0] == 2 else 0)   # EWAH: one marker per maximal fill run
    return lit, g, lit + markers

def sim(m, w, d, trials):
    tl = tg = te = 0
    for _ in range(trials):
        bits = [1 if random.random() < d else 0 for _ in range(m)]
        l, g, e = wah_counts(bits, m, w); tl += l; tg += g; te += e
    return tl/trials, tg/trials, te/trials

for w in (8, 64):
    for d in (0.001, 0.01, 0.05, 0.2, 0.5, 0.8, 0.99):
        m = w*4000; n = m//w
        l, g, e = sim(m, w, d, 6)
        p0 = (1-d)**w; p1 = d**w
        print(f"  w={w:<3} d={d:<6} n={n} l+g_sim={l+g:>9.1f} doc_n[1-p0^2-p1^2]={n*(1-p0**2-p1**2):>9.1f} "
              f"exact_n-(n-1)(..)={n-(n-1)*(p0**2+p1**2):>9.1f} ewah_words={e:>9.1f}")
ok = True
for w in (8, 64):
    for d in (0.001, 0.01, 0.05, 0.2, 0.5, 0.8, 0.99):
        m = w*20000; n = m//w
        l, g, e = sim(m, w, d, 3)
        p0=(1-d)**w; p1=d**w; exact = n - (n-1)*(p0**2+p1**2)
        if abs((l+g) - exact) > 0.02*max(exact,1.0) + 3: ok = False
check("P8 W: simulated l+g matches n-(n-1)(p0^2+p1^2) within 2%", ok)
m, w = 12, 3; n = m//w; ok = True
for d0 in [F(1,4), F(1,2), F(2,3)]:
    e = F(0)
    for bits in itertools.product([0,1], repeat=m):
        k = sum(bits); l,g,_ = wah_counts(list(bits), m, w); e += d0**k*(1-d0)**(m-k)*(l+g)
    p0 = (1-d0)**w; p1 = d0**w
    if e != n - (n-1)*(p0**2+p1**2): ok = False
check("P8 W exact closed form n-(n-1)(p0^2+p1^2) verified exhaustively (m=12,w=3)", ok)

# ---------------------------------------------------------------- P9
print("\n=== P9 complement symmetry ===")
ok_r = ok_rule = ok_w = True
for m in range(1, 17):
    for mask in range(1 << m):
        bits = [(mask >> i) & 1 for i in range(m)]; cbits = [1-x for x in bits]
        r, rc = nruns(bits), nruns(cbits)
        if abs(r-rc) > 1: ok_r = False
        if rc != r - 1 + (0 if bits[0] else 1) + (0 if bits[-1] else 1): ok_rule = False
for m, w in [(16,4),(16,8),(12,4),(8,2)]:
    for mask in range(1 << m):
        bits = [(mask >> i) & 1 for i in range(m)]; cbits = [1-x for x in bits]
        if wah_counts(bits,m,w)[:2] != wah_counts(cbits,m,w)[:2]: ok_w = False
check("P9 |r(X^c)-r(X)| <= 1", ok_r)
check("P9 exact rule r(X^c) = r(X) - 1 + [0 notin X] + [m-1 notin X]", ok_rule)
check("P9 kappa_W(X^c) = kappa_W(X) exactly, when w | m", ok_w)
bad = []
for m, w in [(10,4),(13,4),(9,8)]:
    for mask in range(1 << m):
        bits = [(mask >> i) & 1 for i in range(m)]; cbits = [1-x for x in bits]
        if wah_counts(bits,m,w)[:2] != wah_counts(cbits,m,w)[:2]: bad.append((m,w)); break
check("P9 kappa_W symmetry FAILS when w does not divide m", len(bad) > 0, f"at (m,w)={bad}")

# ---------------------------------------------------------------- Theorem B
print("\n=== Theorem B: attainable range of kappa_R ===")
ok = True
for m in range(1, 15):
    for k in range(1, m+1):
        rs = set()
        for S in itertools.combinations(range(m), k):
            bits = [0]*m
            for i in S: bits[i]=1
            rs.add(nruns(bits))
        if rs != set(range(1, min(k, m-k+1)+1)): ok = False; print("  ", m, k, sorted(rs))
check("Thm B kappa_R attains EVERY integer in [1, min(k, m-k+1)], exhaustive m<=14", ok)

m, w = 10**6, 64
for d in [1e-4, 1e-3, 1/64, 0.1, 0.5]:
    k = int(d*m)
    blk = [0]*m
    for i in range(k): blk[i] = 1
    lb, gb, _ = wah_counts(blk, m, w)
    stride_doc = math.ceil(1/d); stride_fix = m//k
    spread_fix = [i*stride_fix for i in range(k)]
    bits = [0]*m
    for i in spread_fix: bits[i] = 1
    lf, gf, _ = wah_counts(bits, m, w)
    print(f"  d={d:<9} k={k:<7} X_block kappa_W={lb+gb:<3} | doc stride ceil(1/d)={stride_doc} gives |X|={len(range(0,m,stride_doc))}"
          f" {'OK' if len(range(0,m,stride_doc))==k else '<-- WRONG DENSITY'} | fixed stride floor(m/k)={stride_fix} kappa_W={lf+gf} min(k,m/w)={min(k,m//w)}")

# ---------------------------------------------------------------- P10
print("\n=== P10 structure ratio sigma_R ===")
ok_doc = ok_gen = True
for m in range(2, 15):
    for k in range(1, m+1):
        E = F(k*(m-k+1), m)
        for S in itertools.combinations(range(m), k):
            bits=[0]*m
            for i in S: bits[i]=1
            sig = E/nruns(bits)
            if k <= m/2 and sig < F(m-k+1, m): ok_doc = False
            if sig < max(F(k,m), F(m-k+1,m)): ok_gen = False
check("P10 sigma_R >= (m-k+1)/m for d<=1/2 (as stated)", ok_doc)
check("P10 general: sigma_R >= max(d, 1-d+1/m) >= 1/2  (stronger; covers d>1/2)", ok_gen)

# ---------------------------------------------------------------- P11
print("\n=== P11 binned refinement ===")
ok_c = ok_w2 = ok_z = ok_z_eq = True
for trial in range(3000):
    beta = random.choice([2,3,4,5]); nb = random.randint(1,6); m = beta*nb
    A = set(random.sample(range(m), random.randint(0,m))); B = set(random.sample(range(m), random.randint(0,m)))
    a,b = len(A),len(B)
    ak = [len([x for x in A if x//beta==j]) for j in range(nb)]
    bk = [len([x for x in B if x//beta==j]) for j in range(nb)]
    lo = sum(max(0, ak[j]+bk[j]-beta) for j in range(nb)); hi = sum(min(ak[j],bk[j]) for j in range(nb))
    if not (lo <= len(A&B) <= hi): ok_c = False
    if not (max(0,a+b-m) <= lo and hi <= min(a,b)): ok_c = False
    wref = sum(min(ak[j],bk[j],beta-ak[j],beta-bk[j]) for j in range(nb))
    if wref != hi-lo or wref > min(a,b,m-a,m-b): ok_w2 = False
    occ = sum(1 for j in range(nb) if ak[j]>0 and bk[j]>0)
    nondeg = sum(1 for j in range(nb) if min(ak[j],bk[j],beta-ak[j],beta-bk[j]) > 0)
    if nondeg > occ: ok_z = False
    if nondeg != occ: ok_z_eq = False
check("P11 refined interval contains the answer and is inside the global interval", ok_c)
check("P11 refined width = sum of per-bin widths <= W_unc", ok_w2)
check("P11 popcount(occ_A & occ_B) >= #bins with non-degenerate refined interval", ok_z)
check("P11 popcount EQUALS that count  <-- doc claims equality", ok_z_eq,
      "counterexample: a bin FULL on both sides is occupied but its refined interval is a point")

# ---------------------------------------------------------------- P12 / Sec 10
print("\n=== P12 and Section 10 worked numbers ===")
m, w = 10**6, 64
check("kappa_B = m/w = 15625", ceil(m/w) == 15625)
check("P12 best list cell / B*B at s=1/2 is w/2 = 32", abs((m/2)/(m/w) - w/2) < 1e-9)
check("P12 E[r]=m/4 at d=1/2 and E[r]/kappa_B = 16", abs((m*0.25)/(m/w) - 16) < 1e-9)
print(f"  Thm A ratio at s=1/2 = {1/(w*0.5):.5f}  -- below one, NOT 'negative'")
d = 1e-4; a=b=100
print(f"  case A: width={m*d:.0f}, answers={int(m*d)+1}, E|AB|={m*d*d:.4f}, headroom={1/(w*d):.2f}, E[r]={m*d*(1-d):.2f}, "
      f"Pr[disj] in [{exp(-a*b/(m-a-b+1)):.6f},{exp(-a*b/m):.6f}]")
d = 0.51
print(f"  case D: floor={(2*d-1)*m:.0f} ({100*(2*d-1):.0f}% of universe), operand frac {100*(2-1/d):.2f}%, "
      f"ceiling={d*m:.0f}, width={(1-d)*m:.0f} == m*s={m*min(d,1-d):.0f}")

print("\nFAILS:", FAILS if FAILS else "none")
```

**Output (abridged).** All PASS except the two deliberate refutations:
`Corollary A upper half E[r] <= m s` → **38 violations**, and
`P11 popcount EQUALS that count` → **FAIL**. `P7 lower bound FAILS when a+b>m` reports **57 155**
violating triples for `m ≤ 69`.

### 5.2 `verify2.py`

```python
#!/usr/bin/env python3
"""Round 2: the items round 1 left open."""
import itertools, random, math
from fractions import Fraction as F
from math import ceil
random.seed(7)

print("=== (a) Theorem B's X_spread: 'every ceil(1/d)-th position' ===")
m = 10**6
for d in [0.3, 0.45, 0.0003, 0.07, 1/3, 0.2]:
    k = round(d*m); stride = math.ceil(1/d)
    n_doc = len(range(0, m, stride)); stride2 = m//k
    print(f"   d={d:<8.5f} k={k:<7} ceil(1/d)={stride:<6} |X_doc|={n_doc:<7} density={n_doc/m:<9.5f}"
          f"  {'OK' if n_doc==k else 'WRONG DENSITY'}   floor(m/k)={stride2} -> |X|={len(range(0,k*stride2,stride2))}")

print("\n=== (b) max attainable kappa_W at fixed (m,k) ===")
m, w = 20, 4
for k in range(1, 11):
    vals = set()
    for S in itertools.combinations(range(m), k):
        bits = [0]*m
        for i in S: bits[i] = 1
        kinds = []
        for j in range(m//w):
            c = bits[j*w:(j+1)*w]; s = sum(c)
            kinds.append(0 if s == 0 else (1 if s == w else 2))
        lit = sum(1 for x in kinds if x == 2)
        g = sum(1 for j, x in enumerate(kinds) if x != 2 and (j == 0 or kinds[j-1] != x))
        vals.add(lit+g)
    print(f"   k={k:<3} kappa_W in [{min(vals)},{max(vals)}]  n={m//w}  min(k,n)={min(k,m//w)}  bound min(n,2k+1)={min(m//w,2*k+1)}")

print("\n=== (c) EWAH expectation, 25-trial Monte Carlo at n=20000 ===")
def wah(bits, m, w):
    n = (m+w-1)//w; kinds = []
    for j in range(n):
        c = bits[j*w:(j+1)*w]; c = c + [0]*(w-len(c)); s = sum(c)
        kinds.append(0 if s == 0 else (1 if s == w else 2))
    lit = sum(1 for x in kinds if x == 2)
    g = sum(1 for j, x in enumerate(kinds) if x != 2 and (j == 0 or kinds[j-1] != x))
    return lit, g, lit + g + (1 if kinds and kinds[0] == 2 else 0)
for w in (8, 64):
    for d in (0.001, 0.01, 0.05, 0.2, 0.5, 0.9, 0.99, 0.999):
        n = 20000; m = n*w; T = 25; tot = 0
        for _ in range(T):
            bits = [1 if random.random() < d else 0 for _ in range(m)]
            l, g, _ = wah(bits, m, w); tot += l+g
        sim = tot/T; p0 = (1-d)**w; p1 = d**w
        exact = n - (n-1)*(p0**2+p1**2); doc = n*(1-p0**2-p1**2)
        print(f"  w={w:<3} d={d:<6} sim={sim:>9.1f}  exact={exact:>9.1f}  doc={doc:>9.1f}  rel={abs(sim-exact)/exact:.4f}")

print("\n=== (d) monotonicity of E[kappa_W] in s, and its sparse limit ===")
w = 64; prev = -1; mono = True
for i in range(1, 5001):
    s = i/10000.0; v = (1/w)*(1-(1-s)**(2*w)-s**(2*w))
    if v <= prev - 1e-18: mono = False
    prev = v
print("  strictly increasing on (0,1/2]:", mono)
for s in [1e-6, 1e-4, 1e-3, 0.01, 0.05, 0.5]:
    v = (1/w)*(1-(1-s)**(2*w)-s**(2*w))
    print(f"    s={s:<8} E[k_W]/m={v:.6e}   2s={2*s:.6e}   1/w={1/w:.6e}")

print("\n=== (e) envelope vs min(1/w, s), and the Corollary A contradiction ===")
bad = []
for i in range(1, 501):
    s = i/1000.0
    env = min(1/w, s, s*(1-s), (1/w)*(1-(1-s)**(2*w)-s**(2*w)))
    if not (0.5*min(1/w,s) - 1e-15 <= env <= min(1/w,s) + 1e-15): bad.append(s)
print("  envelope within [ref/2, ref] of min(1/w,s) for all s in (0,1/2]:", not bad)
for s in [1e-6,1e-4,1e-2,0.1,0.5]:
    vals = [1/w, s, s*(1-s), (1/w)*(1-(1-s)**(2*w)-s**(2*w))]
    print(f"    s={s:<8} max/min over the FULL matrix = {max(vals)/min(vals):.1f}   <-- 'at most a constant' is false")

print("\n=== (f) exact Bernoulli E[r] is NOT a function of s ===")
m = 1000
for d in [0.1, 0.3, 0.49]:
    print(f"   d={d}: E[r]={d+(m-1)*d*(1-d):.4f} vs d'=1-d: E[r]={(1-d)+(m-1)*(1-d)*d:.4f}  diff={1-2*d:.4f}")

print("\n=== (g) sigma_R denominator: Bernoulli vs fixed-cardinality null ===")
m = 10**6
for d in [1e-4, 0.01, 0.5]:
    k = int(d*m)
    print(f"   d={d}: E_fixed={k*(m-k+1)/m:.4f}  E_bern={d+(m-1)*d*(1-d):.4f}")

print("\n=== (h) P9 boundary conditions for r ===")
m = 8; tab = {}
def nr(b):
    r = 0; p = 0
    for x in b:
        if x and not p: r += 1
        p = x
    return r
for mask in range(1 << m):
    bits = [(mask >> i) & 1 for i in range(m)]
    tab.setdefault((bits[0], bits[-1]), set()).add(nr([1-x for x in bits]) - nr(bits))
for key in sorted(tab): print(f"   (0 in X, m-1 in X) = {key}:  r(X^c)-r(X) in {sorted(tab[key])}")

print("\n=== (i) Theorem A when w does not divide m ===")
for m in [1000, 100, 65]:
    w = 64; s = 0.01
    print(f"   m={m}: ceil(m/w)/(m s)={ceil(m/w)/(m*s):.5f}  1/(w s)={1/(w*s):.5f}  rel err {abs(ceil(m/w)/(m*s)-1/(w*s))*w*s:.4f}")
```

---

## 6. What I could not verify

1. **`§9.1`'s membership-probe lower bound.** I disproved its tightness but did not attempt to prove
   or refute `Ω(m − max(a,b))` as a valid bound. It needs a citation or a proof; as written it is a
   tier-1 recalled claim.
2. **Per-item constants `c_p`, `c_w`, `V`.** By construction unmeasurable from this document; the
   crossover `s*` is correctly slotted as `\nm{}`.
3. **The heuristic that clustering raises `Pr[disjoint]`** above the uniform prediction. It is
   correctly labelled a heuristic. I note it is *not* implied by the stated mechanism: raising the
   variance at fixed mean pushes mass toward both tails, which raises `Pr[=0]` but the magnitude
   depends entirely on the clustering model. Keep the label.
4. **Anything about real corpora.** `σ_ρ` is a definition here; every reported value is `\nm{}`-slotted
   and correctly so.
5. **The figure spec's rescaling constant** in Fig. 2c. The requirement that it be "a single global
   constant, not fitted per series" is the right discipline and is checkable only once data lands.

---

## 7. Recommended edit order

| Priority | Item | Where |
|---|---|---|
| 1 | Fix "selection would buy at most a constant" (§2.1) | `THEORY.md` §5 Cor A; §13.3 Fig. 2b caption |
| 2 | Replace `X_spread` with `{i·⌊m/k⌋}` (§2.2) | `THEORY.md` §6 Thm B proof; `theory_block.tex` l. 74–75 |
| 3 | Strike "derived prediction" and "the argument for measuring rather than modelling" (§2.3, §2.4) | `THEORY.md` §6 Cor B; `theory_block.tex` l. 88–89 |
| 4 | Replace "requires" with a two-achievable-costs phrasing (§2.6) | `THEORY.md` §0; `theory_block.tex` l. 3–4; `NARRATIVE.md` §1c (3 sites) |
| 5 | Add hypothesis `a+b ≤ m` to P7's lower bound | `THEORY.md` §4; `theory_block.tex` Eq. (6) |
| 6 | Add missing Proposition 5b (rank index ⇒ `B×R` costs `2r`) (§4) | `THEORY.md` §3 |
| 7 | `E[r] ≤ m·s + 1`; `w \| m` side conditions on Thm A and P9(`W`); P11 `≤` not `=` | `THEORY.md` §§5, 7; `theory_block.tex` l. 67 |
| 8 | Generalise P10 to `σ_R ≥ max(d, 1−d+1/m)` | `THEORY.md` §6; `theory_block.tex` l. 80 |
| 9 | Rename `W_unc`; fix `0.0721`→`0.0720`; "negative"→"below one"; §9.4 self-contradiction | `THEORY.md` |
| 10 | Restate `NARRATIVE.md` claim **A2** so Theorem B is not a counterexample to it (§4) | `NARRATIVE.md` §5 ledger |
