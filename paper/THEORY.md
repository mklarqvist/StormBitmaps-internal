# Theory — why the method works at all

**Purpose, stated once.** This document is an **existence argument for exploitable headroom**. It is
not a characterisation of representations, not a model of any kernel, and not a prediction of the
empirical curves. It establishes a chain:

1. Given only the operand cardinalities, the answer is confined to an interval of width
   `m·min(s_A, s_B)` — small at both density tails (P2).
2. That interval width is **achievable as work**: an exact algorithm exists costing `O(m·s)` probes
   (P5).
3. A dense bitmap pays **Θ(m/w) unconditionally**. It never adapts to either fact.
4. Therefore the gap between **two achievable exact costs** — what a dense-bitmap kernel spends and
   what a cheaper equally exact algorithm on the same operands already achieves — is **provable, not
   empirical**, and widens without bound as either operand leaves density ½. *That is the headroom.*
   (Theorem A.)
5. **Descriptor length converts into operation count.** A rank index turns a run container's `r` runs
   into `2r` lookups, independent of `m`, of `|A|` and of run length (P5b). This is the bridge from
   "shorter descriptor" to "less work", and without it nothing below bears on cost at all.
6. **Structure is not visible in density.** At fixed density the run count ranges over `[1, k]`
   (Theorem B), so a representation can finish arbitrarily far inside the headroom for reasons no
   cardinality statistic can predict.
7. **The mechanisms do not stack.** Filtering *concentrates* what it passes on, so a zone map
   composed with a representation is not the product of the two: the composition is floored at the
   summary's own overhead and hands the kernel a denser problem than the row's global density
   suggests. Three regimes follow, with computable boundaries (§7, P11b–P11c).
8. The **mid-band is where the headroom provably vanishes** (in the generic case), so a method that
   gets out of the way there is behaving correctly rather than failing.

Steps 1–4 prove there must be something to win. Step 5 makes the win a statement about work rather
than about storage. Step 6 explains why a representation matrix — rather than a formula — is needed
to collect it. Step 7 shows the pieces of that matrix must be *selected between* rather than
composed. Step 8 bounds where. The measurement campaign then reports how much of the available
headroom is actually captured — a claim about our kernels, not about these bounds.

**Note on "requires".** Nothing here lower-bounds the work any algorithm must do. Every comparison
below is between costs that are each *achieved* by an exact algorithm. The word "requires" and its
relatives ("unavoidable", "irreducible", "must spend") are therefore avoided throughout, and §9.1
says why.

The one-sentence thesis this serves (`NARRATIVE.md` §2): *a dense bitmap costs the same no matter
what it contains; equivalent representations of the same set do not; the paper exploits that
deviation.*

**A word that must not be lost: *equivalent*.** Every representation here is exact and
interconvertible. `B`, `S`, `R`, `W` and the complement view `C` encode the same set and every cell
returns the same cardinality. Nothing is approximated, no contract is weakened, no result is
probabilistic. Only the cost changes. Sections 5–7 below are entirely about cost and say nothing
about correctness, because there is nothing to say.

---

## 0. Setup and notation

Universe `[0, m)` of `m` positions. `A, B ⊆ [0,m)` with `a = |A|`, `b = |B|`, densities
`d_A = a/m`, `d_B = b/m`. Machine word width `w` bits (`w = 64` throughout the implementation);
`n = ⌈m/w⌉` words. Vector width `V` words per SIMD operation.

**Effective sparsity** `s_X = min(d_X, 1 − d_X) ∈ [0, ½]`. Write `s = min(s_A, s_B)` when the pair is
meant.

For a representation `ρ` and a set `X`, write `κ_ρ(X)` for the **descriptor length** — the number of
stored items a kernel must touch to traverse `X` in that representation:

| Sym | Representation | `κ_ρ(X)` | 
|---|---|---|
| `B` | dense bitmap | `n = ⌈m/w⌉` words |
| `S` | sorted array of set positions | `k = |X|` |
| `R` | run container, maximal runs `(start, len)` | `r(X)` = run count |
| `W` | WAH/EWAH literal + fill stream | `ℓ(X) + g(X)` (dirty words + fill groups) |
| `C∘ρ` | the same representation over `Xᶜ` | `κ_ρ(Xᶜ)` |

All costs below are counted in **descriptor items touched**, not in nanoseconds. Converting to time
requires per-item constants, which are measured, not derived; every place a constant is needed is
flagged.

---

## 1. What cardinality alone determines

### Proposition 1 (Fréchet interval; exact)

For all `A, B ⊆ [0,m)` with `|A| = a`, `|B| = b`:

```
max(0, a + b − m)  ≤  |A ∩ B|  ≤  min(a, b),
```

and every integer in that range is attained by some pair with those cardinalities.

**Proof.** Upper: `A ∩ B ⊆ A` and `A ∩ B ⊆ B`. Lower: `|A ∪ B| = a + b − |A ∩ B| ≤ m`, so
`|A ∩ B| ≥ a + b − m`; and `|A ∩ B| ≥ 0` trivially. Attainment: fix `t` in the range and take
`A = [0, a)`, `B = [a − t, a − t + b)`. Then `A ∩ B = [a − t, a)`, of size `t` — the construction is
legal because `t ≤ a` gives `a − t ≥ 0` and `t ≥ a + b − m` gives `a − t + b ≤ m`, and `t ≤ b` makes
`[a−t, a)` a subset of `B`. ∎

### Proposition 2 (interval width; exact, and this is the clean form)

The width `U(a,b) := min(a,b) − max(0, a+b−m)` satisfies

```
U  =  min(a, b, m − a, m − b)  =  m · min(s_A, s_B)  =  m·s.
```

**Proof.** `min(a,b) − max(0, a+b−m) = min(a,b) + min(0, m−a−b) = min( min(a,b), min(a,b)+m−a−b )`,
and `min(a,b) + m − a − b = m − max(a,b)`. Hence the width is `min( min(a,b), m − max(a,b) )`, which
is `min(a, b, m−a, m−b)`. Grouping the four terms in pairs,
`min( min(a, m−a), min(b, m−b) ) = min(m·s_A, m·s_B) = m·min(s_A, s_B)`. ∎

**The number of feasible answers is `U + 1`**, so a cardinality-only view of the pair leaves
`log₂(m·s + 1)` bits of the answer unresolved.

**Intuition a systems reader can hold.** The four numbers `a`, `b`, `m−a`, `m−b` are exactly the
sizes of the four lists you could enumerate: `A`, `B`, `Aᶜ`, `Bᶜ`. The uncertainty is the size of the
*smallest* of them. That is not a coincidence — §3 shows the smallest of those four lists is
precisely what you enumerate to answer the query, so **the residual uncertainty and the work of the
cheapest list-based cell are the same number**.

**Note the asymmetry that matters for the paper's framing.** The width depends on `min(s_A, s_B)` —
the *sparser* operand in effective terms. One near-empty (or near-full) side is enough to collapse
the interval. That is the formal content of "on skewed data most pairs have a near-empty side."

**Sketch check.** `NARRATIVE.md` §1c states the width only for `a = b = k`, where it is `m·s`. That
is correct and is the `s_A = s_B` case of Proposition 2. The general form is `m·min(s_A,s_B)`, and it
is worth stating in the paper because it is strictly more useful: it says a pair is cheap when
*either* side is extreme, not only when both are.

### Proposition 3 (pigeonhole floor; exact)

The lower bound lifts off zero **exactly when `d_A + d_B > 1`**, and then

```
|A ∩ B| ≥ (d_A + d_B − 1)·m,   and for d_A = d_B = d:   |A ∩ B| ≥ (2d − 1)·m.
```

**Proof.** Immediate from Proposition 1. Equivalently and more informatively: by inclusion–exclusion
on complements, `|A ∩ B| = a + b − m + |Aᶜ ∩ Bᶜ|`, so the floor is the case `Aᶜ ∩ Bᶜ = ∅` — *the
complements can be at most disjoint*. ∎

**Arithmetic check the brief asked for.** At `d = 0.51` the floor is `(2·0.51 − 1)·m = 0.02·m`.
**The factor is 2, so 51 % density forces 2 % of the universe to overlap, not 1 %.** As a fraction of
either operand it is `(2d−1)/d = 2 − 1/d ≈ 3.92 %`. `NARRATIVE.md` §1c already states `0.02m` and
"two per cent", so the narrative is correct as written; the "1 %" version the brief flags was an
earlier slip and must not reappear.

**One wording defect in `NARRATIVE.md` §1c to fix.** It says "the interval closes at rate `m(1−d)`".
`m(1−d)` is the interval *width* above `d = ½`, not a rate. The correct statement: above `d = ½` the
floor rises at `2m` per unit density and the ceiling `min(a,b) = dm` rises at `m`, so the width
`m(1−d)` shrinks at the constant rate `m` per unit density. (Both are consistent with Proposition 2:
`m(1−d) = m·s` for `d ≥ ½`.)

---

## 2. What a dense bitmap spends

### Proposition 4 (fixed cost; exact, and this is the whole opportunity)

`κ_B(X) = ⌈m/w⌉` for every `X`. The `B×B` cell performs `⌈m/w⌉` AND-plus-popcount word operations
(`⌈m/(wV)⌉` vector operations) for every pair, independently of `a`, `b`, of any structure in either
operand, and of the answer.

This is not a defect of any implementation; it is the definition of the representation. `B` is the
only member of the matrix whose descriptor length is a function of the universe alone.

---

## 3. The headroom

### Proposition 5 (achievability of the interval width; exact)

Suppose each operand `X` is available as a dense bitmap **and** as a sorted array of whichever of
`X`, `Xᶜ` is the smaller — that is `Θ(m·s_X)` extra space, and which one to build is decided from
`a`, `b`, `m` in `O(1)`. Then `|A ∩ B|` is computed in `O(U) = O(m·s)` constant-time operations.

*Hypotheses, stated precisely because an earlier version got them wrong in both directions.* No rank
index is needed here — every case below uses only membership probes into a bitmap and the two
cardinalities. (Rank enters at P5b, for a different cell.) And "a sorted array of the complement as
well" would be `Θ(m)` space; the correct hypothesis stores only the smaller of the two, which is what
the implementation does. Without a *stored* complement array, enumerating `Xᶜ` from the bitmap costs
`Θ(m/w + (m−a))`, whose first term is already `κ_B` — so the dense arm of the argument genuinely
requires the complement to be materialised, not derived on the fly.

**Proof.** Four cases, one per term of `min(a, b, m−a, m−b)`.

- `U = a`: enumerate `A`'s `a` positions, probe each in `B`'s bitmap. `a` probes.
- `U = b`: symmetric.
- `U = m − a`: enumerate `Aᶜ`; `|A ∩ B| = b − |Aᶜ ∩ B|`. `m − a` probes.
- `U = m − b`: symmetric; `|A ∩ B| = a − |A ∩ Bᶜ|`.

Each case is `O(1)` arithmetic plus `U` probes. ∎

**This proposition is the justification for the `C` strategy, and it should be stated that way in the
paper.** Two of the four cases *require* a complement view. The complement is not an optimisation
bolted on for the dense tail; without it the achievable cost is `min(a,b)`, which is `Θ(m)` at high
density, and the dense arm of the U does not exist.

### Proposition 5b (rank index: descriptor length becomes operation count; exact)

**This is the bridge, and without it nothing else in this document bears on cost.** Every other
result here is about descriptor *length*, which is a fact about storage. P5b is the only statement
that converts a descriptor length into a count of operations.

Let `A` be a dense bitmap carrying an `O(1)` rank index, `rank_A(i) = |A ∩ [0,i)|`, and let `B` be a
run container with maximal runs `{[u_i, v_i)}` for `i = 1 … r`, `r = r(B)`. Then

```
  |A ∩ B|  =  Σ_{i=1}^{r} ( rank_A(v_i) − rank_A(u_i) ),
```

computed in exactly `2r` rank lookups and `2r − 1` additions — **independent of `m`, of `|A|`, of
`|B|`, and of the run lengths `v_i − u_i`**.

**Proof.** The maximal runs partition `B`, so `A ∩ B` is the disjoint union of `A ∩ [u_i, v_i)`, and
`|A ∩ [u_i, v_i)| = rank_A(v_i) − rank_A(u_i)` by the definition of rank. Summing gives the identity.
Each term costs two lookups, each `O(1)` by hypothesis. ∎

**Corollary.** `R×R` by simultaneous merge of two sorted run lists costs `O(r_A + r_B)` — no rank
index needed, since the merge never inspects a position inside a run.

**Intuition.** A run is an interval, and a rank index answers "how many set bits before here?" in one
step. So a run of any length is priced the same as a run of length one: two questions and a
subtraction. The kernel never looks *inside* a run. This is why `κ_R` is a cost parameter and not
merely a storage parameter, and it is the exact mechanism the paper's `B×R` cell implements.

**Scope, and it matters.** P5b converts descriptor length into *operation count*, not into time. The
per-operation constants still differ across cells — a rank lookup is not a vector word — and no
timing claim follows from this proposition. What it removes is the far larger gap between "this
representation is small" and "this kernel is fast", which is a gap about asymptotics, not constants.

**Relation to the measured claim.** The paper's A7 ("a rank index makes `B×R` cost `Θ(runs)`,
independent of run length") is the measured confirmation of P5b, and should be presented in that
order — derived, then confirmed — rather than as an empirical discovery. It also retroactively
justifies the rank index appearing in this section at all, since P5 does not use one.

### Theorem A (headroom; exact, given the two propositions above)

Comparing descriptor items touched, the ratio of the fixed-cost kernel's work to the work of the
cheapest list-based cell is

```
      κ_B                ⌈m/w⌉              1
  ──────────────  =  ───────────────  =  ───────  ,
    U              m·min(s_A,s_B)      w·s
```

which exceeds 1 whenever `s < 1/w` and is **unbounded as `s → 0`**.

**Hypothesis: `w | m`.** The middle equality is `⌈m/w⌉ = m/w`, which needs `w` to divide `m` (true of
the implementation, which pads to a whole number of words). Otherwise read the ratio as `≥` with an
additive `w/m` term; relative error at `m = 1000, w = 64` is 2.4 %, at `m = 65` it is 97 %. Nothing
downstream changes.

**Proof.** Proposition 4 gives the numerator, Propositions 2 and 5 the denominator. ∎

**What this does and does not assert.** It asserts that a *specific, exact, correct* algorithm exists
whose cost is `m·s` items where `B×B` spends `m/w` items, and that the ratio between those two
algorithms diverges. It does **not** assert that `m·s` is a lower bound on the work any algorithm
must do — see §9.1. Both quantities compared are *achieved*; neither is a floor. The existence
argument needs only the comparison of two achievable costs, and it has it.

**Converting to time requires two measured constants** and the paper must not skip them: the cost of
a random probe into a bitmap, `c_p`, and the cost of a vectorised AND-plus-popcount word,
`c_w/V`. The crossover in effective sparsity is then

```
  s* = c_w / (c_p · w · V)                              [heuristic: constant-factor model]
```

which the campaign measures as `\nm{measured crossover in effective sparsity}` and against which the
independently measured `c_w`, `c_p` supply a consistency check. This is the only quantitative
prediction in this document and it is explicitly a constant-factor argument, not an asymptotic one.

---

## 4. The expected case

Null model, stated because everything below depends on it: `A` is arbitrary and fixed with `|A| = a`;
`B` is drawn uniformly from the `C(m, b)` subsets of size `b`. (Equivalently both drawn independently
and uniformly at their sizes.) This is a **null model of no structure** and no claim is made that real
corpora obey it — its role is to say what "no structure" would look like, so that §6 can measure
departure from it.

### Proposition 6 (mean; exact)

`E|A ∩ B| = ab/m = m·d_A·d_B`, and `|A ∩ B| ~ Hypergeometric(m, a, b)`, with
`Var = b·(a/m)·((m−a)/m)·((m−b)/(m−1))`.

**Proof.** Linearity: `E|A∩B| = Σ_{i∈A} Pr[i ∈ B] = a·(b/m)`. Only exchangeability of `B` is used;
no independence across positions is required. ∎

**Consequence stated in the sketch, and it is correct.** The expected intersection *density* is
`d_A·d_B` — the **product** of the operand densities. Along the equal-density diagonal that is `d²`,
so it falls **quadratically** as operands sparsify, while `κ_B` stays flat. (Off the diagonal it is
bilinear, not quadratic; the sketch's "quadratic" should be read as the equal-density statement it
is.)

### Proposition 7 (disjointness; exact expression, with a two-sided bound)

```
Pr[A ∩ B = ∅] = C(m−a, b) / C(m, b) = Π_{j=0}^{b−1} (1 − a/(m−j)),
```

for all `(m,a,b)`; the upper bound below holds unconditionally, and **the lower bound requires
`a + b ≤ m`**:

```
exp( − ab / (m − a − b + 1) )   ≤   Pr[A ∩ B = ∅]   ≤   exp( − ab/m ).
```

**The hypothesis is not cosmetic.** When `a + b > m` the sets cannot be disjoint, so `Pr = 0`, while
the denominator `m − a − b + 1` is zero or negative and the left-hand expression is undefined or
exceeds 1. Under `a + b ≤ m` the denominator is `≥ 1` and the bound is valid. Since the proposition
is used only in the sparse regime, where `a + b ≪ m`, the hypothesis costs nothing — but it must be
written down.

**Proof.** The product form counts the `b` choices of `B` drawn from the `m−a` positions outside `A`.
Upper bound: each factor `1 − a/(m−j) ≤ 1 − a/m`, so the product is at most `(1−a/m)^b ≤ e^{−ab/m}`.
Lower bound: `−log(1−x) ≤ x/(1−x)`, and for `j ≤ b−1` we have `x_j = a/(m−j) ≤ a/(m−b+1)`, so
`−log Pr ≤ b · a/(m−b+1) / (1 − a/(m−b+1)) = ab/(m−a−b+1)`. ∎

**So `e^{−ab/m}` is not merely an approximation — it is an upper bound.** The relative error in the
exponent is `O((a+b)/m) = O(d_A + d_B)`: the exponential form is accurate precisely in the sparse
regime, which is the regime it is used to reason about. Numerical check at `m = 1000`, `a = b = 50`:
exact `0.0720`, upper bound `e^{−2.5} = 0.0821`, lower bound `e^{−2.775} = 0.0624`. Two different
ratios are worth keeping apart here: the *exact-versus-upper-bound* exponent ratio is `1.053`, which
is what matches the predicted `1 + (a+b)/2m = 1.05`; the ratio of the two *bound* exponents is
`m/(m−a−b+1) = 1.110`, and it is the width of the bracket, not the error of the approximation.

**The `NARRATIVE.md` §1c statement `≈ e^{−ab/m}` therefore needs a side condition**, and the paper
should carry the bound rather than the approximation: it costs one extra symbol and it is exact.

### Corollary (single-bit sets; exact)

Two single-element sets over a universe of width `m` intersect with probability exactly `1/m`
(`a = b = 1`, so `Pr[∅] = (m−1)/m`).

### Consequence for design

When `ab ≪ m`, `Pr[disjoint] → 1`: in the sparse regime an empty result is the **typical** case, not
an edge case. Proving disjointness cheaply is therefore not one trick among several — it is the
single highest-value thing a summary can do. The campaign reports
`\nm{fraction of sampled pairs with an empty intersection in the target regime}` as the observed
counterpart.

**Heuristic remark, flagged as such.** Real corpora are clustered, and clustering at fixed
cardinalities raises `Var|A∩B|` while leaving `E|A∩B|` unchanged under the null model, which pushes
probability mass toward *both* `0` and large values. The uniform model therefore tends to
**understate** how often real pairs are disjoint. This is an intuition, not a theorem, and it must be
labelled as one wherever it appears.

---

## 5. Expected cost of each representation under a null model

Model for this section: each position is set independently with probability `d` (Bernoulli), and
`w | m` so that every word carries `w` live bits. The Bernoulli expectations below are **exact**; the
leading-order forms in the right-hand column are what Corollary A's monotonicity is about. The
fixed-cardinality model (uniform random `k`-subset) agrees to leading order; its exact form is given
for `R`, where it is elementary. **It is not given for `W`**, because word occupancies are not
independent under a fixed-cardinality draw — that expression is not supplied here and should not be
assumed to be the Bernoulli one.

### Proposition 8 (expected descriptor lengths)

| repr | `E[κ_ρ]` (Bernoulli(`d`), exact) | leading order, as a function of `s = min(d,1−d)` |
|---|---|---|
| `B` | `⌈m/w⌉` | **constant** |
| `S` | `md` | `ms` only via `C` (below) |
| `C∘S` | `m(1−d)` | best-of-`S`-or-`C∘S` `= m·s` |
| `R` | `d + (m−1)d(1−d)` | `m·s(1−s)` |
| `W` | `n − (n−1)(p₀² + p₁²)`, `n = m/w`, `p₀=(1−d)^w`, `p₁=d^w` | `(m/w)·[1 − (1−s)^{2w} − s^{2w}]` |

The `B` and `S` rows are exact and are not asymptotic statements at all. The `R` and `W` rows are
exact Bernoulli expectations whose *leading-order* forms are the ones used downstream; the `W` entry
differs from `n[1 − p₀² − p₁²]` by `p₀² + p₁² ≤ 2` words, which is immaterial at any `n` of interest
but is the form to state if the closed form is checked.

**Proofs.**

*`B`, `S`:* by definition.

*`R`:* a maximal run of ones begins at position `i` iff `x_i = 1` and (`i = 0` or `x_{i−1} = 0`).
Summing the indicator expectations gives `E[r] = d + (m−1)·d(1−d)`. Under the fixed-cardinality model
the same argument gives the exact `E[r] = k(m−k+1)/m`; both are `m·d(1−d)(1+O(1/m))`.

*`W`:* let `p₀ = (1−d)^w` and `p₁ = d^w` be the probabilities that a word is all-zero or all-one, and
`n = m/w`. Words are independent because they cover disjoint bit ranges (this is where `w | m` is
used). Dirty (literal) words: `E[ℓ] = n(1 − p₀ − p₁)`. Fill groups: a maximal block of consecutive
all-`v` words begins at word `j` iff word `j` is all-`v` and (`j = 0` or word `j−1` is not all-`v`),
so `E[g_v] = p_v + (n−1)·p_v(1 − p_v)` exactly. Hence

```
E[ℓ + g] = n(1 − p₀ − p₁) + Σ_v [ p_v + (n−1)p_v(1−p_v) ]
         = n − (n−1)(p₀² + p₁²),
```

which to leading order in `n` is `n[1 − p₀² − p₁²]`. (The EWAH marker-word count is `Θ(ℓ + g)`, so
the asymptotics are unaffected by the exact header layout. **Side conditions:** the encoding must
compress *both* all-zero and all-one fills — WAH and EWAH do; an encoder that compresses only zero
fills is not complement-symmetric and the symmetry claims below fail for it. And `w | m`: with a
zero-padded tail word, the tail is never all-ones, which breaks both the independence used here and
the exact symmetry of Proposition 9.)

**Sparse limit, and it is worth stating because it explains the envelope.**
`(1/w)[1 − (1−s)^{2w} − s^{2w}] → 2s` as `s → 0`, so under the null model **`W` costs about twice
`S` in the sparse tail**, not the same. That factor of two, together with saturation onto the `B`
line at mid density, is why `W` never appears on the cost envelope in either tail.

*`C∘ρ`:* substitute `1 − d` for `d`; complementation is an involution on density.

### Proposition 9 (complement symmetry; exact, not just in expectation)

- `κ_S(Xᶜ) = m − κ_S(X)`. **`S` is not complement-symmetric** — this is why the `C` strategy exists.
- **`R` is complement-symmetric by construction**, because runs of ones and runs of zeros alternate
  along `[0,m)`, so `|r(Xᶜ) − r(X)| ≤ 1`. The `±1` is not a two-sided uncertainty for a *given* set:
  it is determined by boundary membership, and the exact rule is

  ```
  r(Xᶜ) = r(X) − 1 + [0 ∉ X] + [m−1 ∉ X]
  ```

  | `0 ∈ X` | `m−1 ∈ X` | `r(Xᶜ) − r(X)` |
  |---|---|---|
  | no | no | `+1` |
  | no | yes | `0` |
  | yes | no | `0` |
  | yes | yes | `−1` |

  Degenerate cases obey it: `X = ∅` gives `r = 0`, `r(Xᶜ) = 1`; `X = [0,m)` gives `r = 1`,
  `r(Xᶜ) = 0`. So a set touching position `0` or `m−1` does change the count, and by a known sign.
- `κ_W(Xᶜ) = κ_W(X)` **exactly, provided `w | m`**, because complementing every word maps all-zero
  words to all-one words and back and maps dirty words to dirty words, leaving the literal/fill
  partition untouched. **This fails when `w ∤ m`**: a zero-padded tail word that is all-ones over the
  live bits is not all-ones over the padded word, so complementation moves it between the literal
  and fill classes.
- `κ_B(Xᶜ) = κ_B(X)` trivially.

**This settles [GAP 1] analytically.** `R×R` winning the dense tail was recorded as incidental; it is
a property of the representation. The paper should say so, and should say in the same breath that
`S`-based cells need an explicit complement view for exactly the reason `R` does not.

### Corollary A — under randomness, the U-shape is a theorem

Each **leading-order** entry of Proposition 8 is, as a function of `s`, either constant (`B`) or
**strictly increasing on `[0, ½]`**:

- best-of-`S`/`C∘S`: `m·s`. Increasing. ✔
- `R`: `m·s(1−s)`, since `d(1−d) = s(1−s)` on both branches. `d/ds[s(1−s)] = 1−2s > 0` for `s < ½`. ✔
- `W`: `(m/w)[1 − (1−s)^{2w} − s^{2w}]`, derivative `(m/w)·2w[(1−s)^{2w−1} − s^{2w−1}] > 0` for
  `s < ½`. ✔

and each is symmetric about `d = ½` by construction.

**"Leading-order" is load-bearing here.** The *exact* Bernoulli `E[r] = d + (m−1)d(1−d)` is **not** a
function of `s`: at `m = 1000` it is `90.01` at `d = 0.1` and `90.81` at `d = 0.9`, differing by
`1 − 2d`, and its maximum sits at `d = ½ + 1/(2(m−1))` rather than at `d = ½` — so it is not even
monotone in `s` over the last `1/m` of the dense branch. Everything involved is `O(1)` and nothing
downstream depends on it. By contrast `E[κ_W]` **is** exactly a function of `s`, since
`(1−d)^{2w} + d^{2w}` is symmetric under `d ↦ 1−d`.

**Scope, corrected — and this is the point at which an earlier version of this document contradicted
its own Theorem A.** The sandwich is

```
  ½·m·s  ≤  E[κ_R]  ≤  m·s + 1,
```

where the `+1` is needed because fixed-cardinality `E[r] = m·s(1−s) + d` and Bernoulli
`E[r] = m·s(1−s) + d²` both exceed `m·s` marginally at the dense corner (`m = 64, k = 57` gives
`E[r] = 7.125` against `m·s = 7`; the worst ratio `E[r]/(m·s)` tends to 2). So under the null model
**the four compressed representations agree to within a constant factor**: `S`-with-complement costs
`m·s`, `R` costs between `½m·s` and `m·s + 1`, and `W` costs about `2m·s` in the sparse tail while
saturating at `m/w`. Selection **among `{S, C∘S, R, W}`** therefore buys at most a constant under
randomness — randomness gives run containers nothing over sorted arrays.

**`B` is the exception, and that exception is the entire point.** `B` is a member of the matrix and
its expected cost is `m/w` at *every* density, so the spread across the full matrix is
`(m/w)/(m·s) = 1/(w·s)`, which is unbounded — that is Theorem A, not a contradiction of it. Concretely,
at `s = 10⁻⁶` the ratio of the most to the least expensive representation in the matrix is over
`10⁴`. What collapses under randomness is the choice *among the compressed representations*; what
does not collapse is the choice *between the bitmap and the rest*.

Correspondingly it is the **envelope**, not every cell, that is `Θ(min(m/w, m·s))` — `κ_B` is `m/w`
regardless. (Descriptor lengths here are per *operand*; a cell's cost is a function of both operands'
representations.) The crossover of that envelope sits at `w·s ≈ 1`, i.e. `s* ≈ 1/w` — about `1.6 %`
for `w = 64` before per-item constants, moving to `\nm{measured crossover in effective sparsity}`
once they are charged.

**This is the reconciliation the two halves of the brief needed.** The U-shape is a genuine theorem —
*of the null model and of the bitmap's flatness*. It is not a property of the method, and the method
is not in the business of reproducing it.

---

## 6. The deviation — where the contribution lives

### Theorem B (density is not a sufficient statistic for cost)

Fix `m` and `d ≤ ½`, and let `k = dm`. Over the sets of density exactly `d`:

- `κ_B` is constant, `= ⌈m/w⌉`.
- `κ_S` is constant, `= k`. (Both are functions of `(m, k)` alone.)
- `κ_R` ranges over **every integer in `[1, min(k, m−k+1)]`**.
- `κ_W` ranges from `O(1)` to `Θ(min(k, m/w))`.

Hence `κ_R` and `κ_W` vary by an **unbounded factor — `Θ(m)` — at fixed density**, while `κ_B` and
`κ_S` do not vary at all.

**Proof (explicit families).** Take `X_block = [0, k)`: one run, so `r = 1`; its words are one
all-one fill of `⌊k/w⌋` words flanked by all-zero fills, so `κ_W = O(1)`.

Take

```
  X_spread  :=  { i·⌊m/k⌋ : 0 ≤ i < k }.
```

Then `|X_spread| = k` **exactly** — the largest element is `(k−1)⌊m/k⌋ < m` — so it has density `d`
as required. The stride `⌊m/k⌋ ≥ 2` whenever `k ≤ m/2`, so every element is isolated and `r = k`.
For `κ_W`: every `w`-bit word contains at least one element iff the stride is `≤ w`, i.e. iff
`d ≳ 1/w`, in which case every word is dirty and `κ_W = m/w`; otherwise the `k` elements fall in
distinct words, giving `k` dirty words separated by `Θ(k)` fill groups and `κ_W = Θ(k)`. That case
split is the same `d` versus `1/w` split the rest of this section uses.

*This construction replaces an earlier one — "every `⌈1/d⌉`-th position" — which does **not** have
density `d` unless `1/d` is an integer: at `d = 0.30` the stride `⌈1/d⌉ = 4` yields density `0.25`.
The theorem was unaffected; its proof was not.*

For the intermediate values, rather than merging elements one at a time it is cleaner to construct
directly: choose any composition of `k` into `r` parts (the run lengths) and any distribution of the
`m − k` zeros into the `r + 1` gaps with the `r − 1` internal gaps non-empty. This is feasible iff
`r ≤ k` and `r ≤ m − k + 1`, which is exactly the claimed range — a run needs at least one element,
and `r` runs need at least `r − 1` separating gaps. ∎

**Verified independently** by exhaustive enumeration over all `k`-subsets of `[0,m)` for every
`m ≤ 14` and every `k`: the attainable run counts are *exactly* `{1, …, min(k, m−k+1)}`, with no gaps
and both endpoints attained.

**The worked case that carries the thesis.** `m = 10⁶`, `A = [0, 5·10⁵)`, `B = [2.5·10⁵, 7.5·10⁵)`.
Both operands sit at density exactly `½` — the worst point of Proposition 2, where cardinality leaves
`500 001` feasible answers and the null model predicts `E[r] = m/4 = 250 000`. Actual `r_A = r_B = 1`.
`B×R` with a rank index answers the query in **two rank lookups**. The bitmap kernel spends
`15 625` word operations to reach the same number. Nothing about the density says which of these two
situations you are in.

### Definition — the structure ratio

For a representation `ρ` and a set `X` of density `d`, define

```
  σ_ρ(X)  :=  E_{null(d)}[ κ_ρ ]  /  κ_ρ(X).
```

`σ_ρ = 1` means the row is exactly as expensive as its density predicts. `σ_ρ ≫ 1` means the row
carries structure the density does not express. `σ_B = σ_S ≡ 1` identically.

**Which null model — pinned here, once.** `σ` is defined against the **fixed-cardinality** null
(`E[r] = k(m−k+1)/m`), not the Bernoulli one, throughout this document, in the §13 schema, and in any
`σ` column the paper reports. The two differ by `d − d² = O(1)`, so nothing numerically material
turns on the choice, but `σ` is a reported quantity and its denominator convention must be fixed in
one place. Fixed-cardinality is the right choice because it conditions on the observable — the row's
own cardinality `k` — and because it is exact.

### Proposition 10 (the deviation is one-sided in the useful direction)

At **every** density,

```
  σ_R(X)  ≥  max(k, m−k+1)/m  =  max( d,  1−d+1/m )  ≥  ½,
```

with equality only for the maximally scattered set; and `σ_R` is unbounded above, reaching `Θ(m)` on
`X_block`.

**Proof.** `σ_R = E[r]/r ≥ E[r]/r_max` with `r_max = min(k, m−k+1)` and `E[r] = k(m−k+1)/m`. Since
`xy/min(x,y) = max(x,y)`, the ratio is `max(k, m−k+1)/m`. It exceeds `½` because
`k + (m−k+1) = m+1 > m`, so the larger of the two exceeds `m/2`. The upper end is Theorem B. ∎

**This is stronger than the `d ≤ ½`-only form an earlier version stated, and the strengthening is
needed.** The restricted bound `1 − d + 1/m` degenerates to `0.002` at `d = 0.999`, which would
suggest a 500× worst case where the truth is `σ_R ≥ 0.999`. The sentence below is stated without a
density restriction, so the bound behind it has to be too.

**Read this out loud, because it is the strongest sentence available to the paper.** Real data can be
arbitrarily cheaper than random data at the same density, and at most about twice as expensive. The
deviation the method exploits is bounded below and unbounded above.

### Corollary B — what density-only selection cannot do

**The impossibility, which is exact.** Any selector whose input is `d` (or `a`, `b`, or any function
of them) evaluates one fixed function `f(d)` on every row of that density. By Theorem B, `κ_R` takes
**every** value in `[1, k]` at density `d = k/m`. Therefore `f(d)` mis-predicts `κ_R` by an unbounded
factor on some rows at every density, in at least one direction, and no such selector can distinguish
`X_block` from `X_spread`. ∎

Two consequences, and the boundary between what this proves and what the paper measures has to be
drawn precisely, because an earlier version of this corollary crossed it.

1. **A fixed representation cannot exploit `σ`** — it commits before `σ` is knowable. This is the
   foil, and it follows.
2. **A density-only cost model cannot exploit `σ` either** — it is a function of `d`, so the
   impossibility above applies to it verbatim. What does **not** follow is that such a model is worse
   than making no selection at all: whether a mis-prediction costs more than a fixed default depends
   on which cell the wrong prediction picks and on that cell's actual cost, neither of which is
   modelled here. Claim **A6** (per-pair model regret against an all-bitmap baseline) is a
   **measurement**, and a good one; the theory motivates it but does not derive it and must not be
   written as though it did.

**What this licenses, and what it leaves open.** The proved statement rules out one class of selector
— those reading only cardinality. It does **not** choose among the survivors. A run count or a
zone-map occupancy count is `O(1)` metadata that sees `σ` perfectly and can perfectly well be fed to
a *model*; probe-and-commit is a different survivor. So the theory says: **gate selection on a
statistic that sees structure rather than on cardinality.** Whether that statistic is a cheap
structural summary or a direct measurement of the candidate kernels is settled by measurement (A5,
A6), not here.

That is a narrower claim than "the theory implies probe-and-commit", and it is the one that is
actually proved. It is still a strong result: it rules out, a priori, the entire family of selectors
the cost model in Methods belongs to, and it does so from a fact about representations rather than
from a benchmark.

**A definitional caution.** `σ_ρ` is defined *against* the null expectation, so "wrong by a factor of
`σ`" is a restatement of the definition, not a further consequence of it. The content is Theorem B —
that the range of `κ_R` at fixed density is `Θ(m)` wide — not the ratio's name.

---

## 7. Two distinct mechanisms, named separately

The brief's item (v) conflated two things that should stay apart. Both are real; they act on
different objects.

### Mechanism I — side information narrows the interval

### Proposition 11 (refinement is monotone; exact)

Partition `[0,m)` into `n_β = m/β` bins. Let `a_k`, `b_k` be the per-bin cardinalities. Then

```
  Σ_k max(0, a_k + b_k − β)  ≤  |A ∩ B|  ≤  Σ_k min(a_k, b_k),
```

the refined interval is contained in the global one of Proposition 1, and its width is
`Σ_k min(a_k, b_k, β−a_k, β−b_k) ≤ U`.

**Proof.** Apply Proposition 1 inside each bin and sum; `|A∩B| = Σ_k |A∩B∩bin_k|`. Containment:
`Σ_k min(a_k,b_k) ≤ min(Σ a_k, Σ b_k) = min(a,b)` and
`Σ_k max(0, a_k+b_k−β) ≥ max(0, Σ_k (a_k+b_k−β)) = max(0, a+b−m)`. The width statement is
Proposition 2 applied per bin. ∎

**More side information never widens the interval.** The three devices of Fig. 1c–e are instances:

| device | what it knows | what it does to the interval | cost |
|---|---|---|---|
| cardinality | `a`, `b` | bounds it globally: width `m·s` | `O(1)` |
| zone map | the indicators `[a_k>0]`, `[b_k>0]` | collapses it to the single point `0` on every bin where the AND is zero | `1/β` of the row |
| rank index | `rank_B` at run endpoints | resolves a run's contribution **exactly**: `rank_B(v) − rank_B(u)` | `O(1)` per run |

This underwrites the zone-map claim in Methods, but the precise relation is an inequality and an
earlier version of this gloss stated it as an equality:

```
  #{ bins with a non-degenerate refined interval }  ≤  popcount(occ_A ∧ occ_B),
```

with equality iff no bin is saturated on both sides. The counterexample is immediate: a bin in which
**both** operands are full is occupied on both sides, so the popcount counts it, yet its refined
interval is the single point `β` — degenerate. The two operationally useful statements are untouched
and are the ones the paper makes: **a popcount of zero proves the operands disjoint**, and **the
popcount is the number of bins a kernel consulting only occupancy will visit** (a kernel that does
not know a bin is saturated must still visit it). What is not true is that the popcount counts bins
whose contribution is genuinely undetermined.

### Mechanism II — structure lets a kernel finish below the density-only prediction

Distinct from Mechanism I and not a special case of it. Mechanism I narrows *what the answer could
be*, given a summary. Mechanism II changes *how many items must be touched* to traverse the same set,
given a different but equivalent encoding of it. Theorem B and Proposition 10 are the content;
`σ_ρ` is the quantity. A run container at `r = 1` does not narrow the interval at all — the answer
is still one of `500 001` values until the rank lookups are done — it simply reaches the answer in
two operations instead of `250 000`.

Keeping these apart matters for the prose, because the paper claims both and they are defended by
different arguments.

### Composing them — and why the composition is not the product

The two mechanisms are usually described as stacking: filter the bins, then run the best
representation on what survives. **They do not compose multiplicatively, and the reason is that
filtering changes the density of what it passes on.**

#### Proposition 11b (concentration; exact)

Partition the universe into bins of width `W_z` and let each position be set independently with
probability `d`. Let `K` be a bin's cardinality and `p = Pr[K ≥ 1] = 1 − (1−d)^{W_z}`. Then

```
  E[ K | K ≥ 1 ]  =  W_z · d / p,          i.e.   E[ local density | bin occupied ]  =  d / p,
```

and

```
  Var[ K | K ≥ 1 ]  =  W_z d(1−d)/p  −  W_z² d² (1−p)/p².
```

**Proof.** `K` is zero on the complement of the conditioning event, so `E[K] = E[K | K≥1]·p`, giving
the mean; the second moment follows the same way from `E[K²] = W_z d(1−d) + W_z²d²`, and the variance
is the difference. ∎

**Both were checked against exhaustive enumeration** of the binomial for `W_z ≤ 12` across
`d ∈ {0.05, 0.2, 0.5, 0.9}`; the mean matched to machine precision and the variance closed form to
machine precision. *The variance was the step flagged as underived, and it behaves in the direction
that helps:* as `d → 0` the conditional distribution collapses onto the single value `1`
(`Var → 0`; coefficient of variation `0.016` at `d = 10⁻⁶`, `W_z = 512`). So in the regime where
concentration matters most, "an occupied bin" means "a bin with exactly one bit" almost surely, and
the conditional mean is not merely a summary of a spread-out distribution.

**Intuition.** Conditioning on occupancy is conditioning on a rare event that requires at least one
bit, so it drags the local density up to at least `1/W_z` no matter how sparse the row is globally.
`d/p → 1/W_z` as `d → 0` and `d/p → d` as `d → 1`. **A zone map cannot hand a kernel anything sparser
than one bit per bin.** That is the whole phenomenon.

#### Proposition 11c (composition, and the three regimes)

Let `κ(d) ∈ (0,1]` be the cheapest-representation envelope of §5 normalised so that `κ = 1` is a
plain bitmap, let `A` and `B` be drawn independently, and let the zone-map pass cost a fraction
`c/W_z` of a plain bitmap scan (`c` counts how many summary streams the pass reads — see the note
below). Then, relative to plain-bitmap cost,

```
  composed(d_A, d_B)  =  c/W_z  +  p_A · p_B · κ( d/p )        [ d_A = d_B = d, p_A = p_B = p ]
```

where the *third* factor is evaluated at the **concentrated** density `d/p` of Proposition 11b, not
at `d`. Two consequences, both exact:

1. **The composition never costs more than the zone map alone**, since `κ ≤ 1` and zone-map-alone is
   `c/W_z + p_A p_B`. Verified over `10⁻⁸ ≤ d ≤ 1`: no violation.
2. **The composition does not dominate the representation alone.** It is floored at `c/W_z`, whereas
   `κ(d) → 0` as `d → 0`. Below that floor the summary costs more than a sparse representation pays
   outright.

**Therefore three regimes, not two**, and the boundaries have a closed form. In the sparse band the
envelope is `κ(d) = w·d`, so the composition beats *both* the representation alone and the bitmap
exactly when

```
  x · e^{−x}  >  c / w,        where   x = W_z · d   is the expected set bits per bin.
```

**This is the cleanest form of the result and it should be the one the paper states**, because the
window is a window in *bits per bin* and is therefore independent of `W_z` and of the universe:

| `c` | window in `x = W_z·d` | at `W_z = 512` | at `W_z = 4096` |
|---|---|---|---|
| 1 | `x ∈ [0.0159, 5.94]` | `d ∈ [3.1×10⁻⁵, 1.2×10⁻²]` | `d ∈ [3.9×10⁻⁶, 1.5×10⁻³]` |
| 2 | `x ∈ [0.0323, 5.09]` | `d ∈ [6.3×10⁻⁵, 9.9×10⁻³]` | `d ∈ [7.9×10⁻⁶, 1.2×10⁻³]` |

Both rows were confirmed against a direct numerical scan of the full envelope (not the sparse
approximation) at `W_z ∈ {256, 512, 1024, 4096}`; predicted and scanned boundaries agree to three
significant figures throughout.

**State the overhead convention when quoting numbers.** `c = 1` counts the single AND-and-popcount
pass over `m/W_z` bits; `c = 2` also charges reading the two summary streams. The figure work used
`c = 2`, which is what produces the quoted `d ∈ [6×10⁻⁵, 9×10⁻³]` at `W_z = 512`. The two
conventions differ by a factor of two in `d_low` and by about 15 % in `d_high`; nothing structural
depends on the choice, but the two sets of numbers are not interchangeable and a reader who
recomputes will land on one or the other.

**The lower boundary has an exact and memorable form**, `d_low = c/(w·W_z)` — verified to four
figures at every `W_z` above. It is precisely the density at which a sorted array's own cost falls
below the zone map's overhead: `w·d = c/W_z`. **The upper boundary is where bins stop being empty**:
at `x ≈ 5` the occupancy `p = 1 − e^{−x}` exceeds `0.99`, the filter removes almost nothing, and its
overhead is pure loss. So the window opens when the summary becomes cheaper than enumerating, and
closes when there is nothing left for it to remove.

**Why this matters to the paper's argument.** It is the analytic form of *the mechanisms must be
selected between, not stacked*. The paper currently argues that only from the C29 gating measurement
— an empirical finding that unconditional filtering is a net loss. Proposition 11c says the same
thing derivably, and says more: it identifies *which* competitor wins on each side (a sparse
representation below the window, the bitmap above it) rather than only that the filter should
sometimes be off. Read positively, the zone map has a band of densities it owns, and the band is
computable in advance from `W_z` and `w` alone.

**Hypotheses, and they are real.** Independence of `A` and `B` (used for `p_A p_B`); Bernoulli
within a bin (used for `p` and for Proposition 11b); and the null-model envelope `κ` of §5, which
Theorem B says real data departs from without limit. On clustered data every one of these fails in
the direction that makes the filter *better* — occupancy becomes correlated and bins become emptier
than Bernoulli predicts — so the window computed here is a conservative, structureless-data
baseline, not a prediction for a corpus.

---

## 8. Where the headroom vanishes

### Proposition 12 (the mid-band, stated with its side condition)

At `s_A = s_B = ½`:

- `U = m/2` (Proposition 2): the cardinality view excludes nothing.
- Best list-based cell: `m/2` probes (Proposition 5) against `B×B`'s `m/w` word operations. `B×B`
  wins by `w/2` — a factor of `32` at `w = 64`, before vectorisation.
- Under the null model, `E[r] = m/4` and `E[κ_W] = m/w·(1 − 2·2^{−2w}) ≈ m/w`: neither `R` nor `W`
  offers anything either, and `W` has degenerated into `B` with header overhead.

So the headroom ratio of Theorem A does not merely shrink at `s = ½` — it **falls below one**
(`1/(w·s) = 1/32`), and the correct behaviour of a selector there is to choose `B×B`. (The ratio is
of course positive; "negative headroom" is the wrong word for a quantity below unity and an earlier
version used it.)

**Side condition, and it must be in the paper.** This is a statement about the *generic* case. It is
false for structured rows at mid density: Theorem B's `X_block` sits at `d = ½` with `r = 1`. The
honest form is: *given only cardinalities, and absent structure, the mid-band is where nothing can be
avoided.* A structural summary can still find something there, which is precisely why the zone map
earns its place — it is the mechanism that operates in the band where cardinality is useless.

**Terminology.** `NARRATIVE.md` §1c calls the mid-band an "information-theoretic floor." That phrase
overclaims: nothing here lower-bounds the work of an arbitrary algorithm (see §9). Call it a
**metadata floor** or an **identifiability floor**, and say what it is a floor on: *what cardinality
metadata can determine*, not *what any kernel must spend*. The rhetorical value — a named worst case,
derived rather than conceded — survives the rename intact.

---

## 9. What this does not say

Guard rails. Every item here is a real over-reading that the prose could invite.

1. **`U` is not a lower bound on work.** Nothing above proves any algorithm must perform `Ω(m·s)`
   operations. Theorem A compares two **achievable** costs, which is all the existence argument
   requires. Do not write "must", "requires", "unavoidable" or "irreducible" of the work itself.

   That `U` is the *wrong* candidate for a lower bound can be seen by choosing a concrete machine
   model, and the one nearest to hand gives a bound with no dependence on `s` at all.

   > **Proposition 13 (membership-probe model; derived here, not recalled).** Let an algorithm know
   > `m`, `a`, `b` in advance, and let its only primitive be a membership probe — "is position `i` in
   > `A`?" or "is `i` in `B`?" — with the requirement that it output `|A ∩ B|` exactly. Then
   > whenever `a ∉ {0,m}` and `b ∉ {0,m}`, it makes at least `m/2` probes in the worst case,
   > **independently of `s`**.
   >
   > *Proof.* Complementing an operand is a bijection on probe sequences (the answer flips) and
   > determines the answer (`|A ∩ B| = b − |Aᶜ ∩ B|`), so assume `a ≤ b ≤ m/2`. The adversary answers
   > "no" to every probe. Let `P_A`, `P_B` be the probed sets, `q_A`, `q_B` their sizes,
   > `q = q_A + q_B`. If consistency has been forced (`m − q_A < a` or `m − q_B < b`) then
   > `q > min(m−a, m−b) ≥ m/2` and we are done. Otherwise, choose `A ⊆ P̄_A ∩ P̄_B` with `|A| = a`,
   > possible when `q ≤ m − a`. The values of `|A ∩ B|` realisable by `b`-subsets `B ⊆ P̄_B` are all
   > integers in `[max(0, b − (m − q_B − a)), min(a,b)]`, which contains two distinct values whenever
   > `q_B < m − b`. So the algorithm cannot have halted while both `q ≤ m − a` and `q_B < m − b`,
   > giving `q ≥ min(m − a + 1, m − b) ≥ m/2` since `a, b ≤ m/2`. ∎

   The bound is `Ω(m)` at *every* density — at `a = b = 1` it forces `m − 1` probes — so it tracks
   nothing about the interval width. The reason is that a probe model cannot read a sorted array,
   which is precisely the capability P5 assumes. **The moral is that the interval width is not a
   lower bound in any model considered here, and the nearest model that does give a lower bound gives
   one that is insensitive to `s`.** A matching bound in a representation-aware model is a
   comparison-complexity question with its own literature and must be sourced before it is cited;
   this document does not cite one. (An earlier version asserted `Ω(m − max(a,b))` here without proof
   or source — a tier-1 recalled claim inside the section that forbids them. It is also not tight:
   at `a = b = m−1` it would assert `Ω(1)` where the proposition above gives `Ω(m)`.)
2. **These are bounds given specific metadata, not statements about any kernel's achieved cost.**
   Every proposition here counts descriptor items or elementary operations. Time is operations times
   a measured constant, and the constants differ by more than an order of magnitude across cells (a
   random probe versus a vector word). P5b crosses from descriptor length to *operation count*; it
   does not cross to time, and nothing here does. No performance claim follows from this document
   alone. **This applies with particular force to §7's composition result and to all three analytic
   panels of Fig. 2, which are models of _work_ — words touched — and of nothing else.**

   > **The specific discrepancy this predicts, recorded so it is not read as a contradiction:
   > [GAP 12] in `NARRATIVE.md` §12.** The analytic zone-map overhead here is bounded by `c/W_z`,
   > about 0.2–0.4 % of a bitmap scan at `W_z = 512`. The project's C29 ablation measured the
   > *ungated* filter at `0.09×` worst case — an 11× slowdown. Those are three orders of magnitude
   > apart, and **they are not in conflict, because they are not measuring the same thing.** The
   > model counts words; the measured penalty is memory-system behaviour — an extra pass over a
   > second array, lost sequentiality, and a data-dependent branch per bin, none of which appears in
   > any word count. This document therefore predicts *where the filter has work to remove*
   > (Proposition 11c's window) and predicts *nothing whatever* about what the filter costs when it
   > removes nothing. Reconciling the two is [GAP 12] and is a measurement task, not an analytic one.
   > What the theory must not be read as saying is that unconditional filtering costs 0.2 %.
3. **The U-shape is a property of the bound and of the bitmap's flatness, not of the method.** Under
   the null model everything tracks `s`; on real data the representations deviate, and the deviation
   is the contribution. Do not present the U as something the method reproduces or should reproduce.
4. **No map from descriptor length to time is claimed for `R` or `W`.** Their *descriptor lengths*
   are `r` and `ℓ+g` — structural parameters, not functions of density, as Theorem B says.
   Proposition 8 gives their expectation under a null model and nothing more. The one place a
   descriptor length becomes an operation count is P5b, and it does so for one specific pairing
   (`B×R` against a rank index) under a stated hypothesis, in units of rank lookups.
5. **The null model is a null model.** Real corpora are not Bernoulli. §5 exists to define what "no
   structure" costs so that §6 can measure departure from it; it is not a description of any corpus.
6. **`σ_ρ` is a definition, not a measurement.** Computing it for a real corpus requires `κ_ρ(X)`,
   which the campaign supplies. Every `σ` reported in the paper is `\nm{}`-slotted.
7. **Nothing here is about union, difference or symmetric difference.** Propositions 1–3 are about
   `|A ∩ B|`. The analogues exist (`|A ∪ B| = a + b − |A ∩ B|` makes the union interval a reflection
   of the intersection interval, so the *width* is identical), but the achievability argument of
   Proposition 5 does not transfer unchanged: an empty operand makes intersection trivial and leaves
   union proportional to the other side. That asymmetry is [GAP 11]'s prediction and it should be
   derived properly when the operation sweep lands, not assumed here.
8. **`m` is the universe, not the data.** Every ratio in Theorem A grows with `m` at fixed
   cardinality. This is the same quantity that makes the selector's metadata universe-proportional
   (L3). The theory that supplies the headline also supplies the defect; say both.
9. **No number in this document is measured.** The only numeric values are algebraic evaluations of
   the propositions (`0.02m`, `w/2 = 32`, `s* ≈ 1/w`) and the arithmetic check in Proposition 7.

---

## 10. Worked numerical examples

Universe `m = 10⁶`, word width `w = 64`, so `κ_B = 15 625` words throughout.

### A — sparse (`d_A = d_B = 10⁻⁴`, `a = b = 100`)

| quantity | value |
|---|---|
| interval | `[0, 100]`, width `100 = m·s` |
| feasible answers | `101` |
| `E|A∩B|` | `ab/m = 0.01` |
| `Pr[disjoint]` | between `e^{−0.010002}` and `e^{−0.01}`; `≈ 99.0 %` |
| `κ_B` | `15 625` words |
| best list cell | `100` probes |
| headroom (Theorem A) | `1/(w·s) = 156×` |
| `E[r]` under null | `≈ 100` — `R` buys nothing over `S` here |

The typical answer is `0`, and the whole computation should be a disjointness proof.

### B — mid-density, unstructured (`d_A = d_B = ½`)

| quantity | value |
|---|---|
| interval | `[0, 500 000]`, width `m/2` |
| feasible answers | `500 001` — cardinality excludes nothing |
| `κ_B` | `15 625` words |
| best list cell | `500 000` probes — `32×` worse than `B×B` |
| `E[r]` under null | `250 000` — `16×` worse than `B×B` |
| `E[κ_W]` under null | `≈ 15 625` — `W` has become `B` |
| headroom | `1/(w·s) = 1/32 < 1`: none |

### C — mid-density, structured (the thesis in one line)

`A = [0, 5·10⁵)`, `B = [2.5·10⁵, 7.5·10⁵)`; both `d = ½`, identical to case B by every cardinality
statistic.

| quantity | value |
|---|---|
| interval from cardinality | unchanged: `[0, 500 000]` |
| `E[r]` under null | `250 000` |
| actual `r_A = r_B` | `1` |
| `σ_R` | `250 000` |
| `B×R` with rank index (P5b, `2r`) | **2 lookups** |
| `κ_B` | `15 625` words |

Cases B and C are indistinguishable to any density-based selector and differ in cost by four orders
of magnitude. Note which results are doing the work here: Theorem B says the two cases can differ at
all, and **Proposition 5b is what makes the difference a difference in operations rather than only in
storage**. Without P5b this table compares a run count against a word count and the comparison is
meaningless.

### D — dense (`d_A = d_B = 0.51`)

| quantity | value |
|---|---|
| pigeonhole floor | `(2d−1)m = 2·10⁴` = **2 % of the universe** (factor 2, not 1) |
| as a fraction of each operand | `2 − 1/d = 3.92 %` |
| ceiling | `5.1·10⁵` |
| width | `m(1−d) = 4.9·10⁵ = m·s` ✔ consistent with Proposition 2 |

At `d = 0.99` the same algebra gives floor `0.98m`, width `0.01m`, and a complement view of `10⁴`
elements per side — comparable to `κ_B = 15 625`, i.e. right at the crossover, which is where
Corollary A predicts it (`s* ≈ 1/w = 1.6 %`). At `d = 0.999` the complement side is `10³` and the
dense arm of the U is open.

---

## 11. Result ledger — exact, approximate, heuristic

| # | Result | Status |
|---|---|---|
| P1 | Fréchet interval, all values attained | **exact** |
| P2 | width `= min(a,b,m−a,m−b) = m·min(s_A,s_B)` | **exact** |
| P3 | pigeonhole floor `(d_A+d_B−1)m`; lifts off iff `d_A+d_B>1` | **exact** |
| P4 | `κ_B = ⌈m/w⌉` unconditionally | **exact** (definitional) |
| P5 | `O(m·s)` achievability, four cases, two needing `C` | **exact**; hypotheses are a bitmap plus a sorted array of the *smaller* of `X`, `Xᶜ`. No rank index used |
| **P5b** | rank index ⇒ `B×R` costs `2r` lookups, independent of `m`, `\|A\|`, `\|B\|`, run length | **exact**; the only bridge from descriptor length to operation count. Still not a claim about time |
| **Thm A** | headroom ratio `1/(w·s)`, unbounded as `s→0` | **exact** given `w \| m`, as a ratio of two achievable costs; otherwise `≥` with an additive `w/m` |
| — | crossover `s* = c_w/(c_p·w·V)` | **heuristic** — constant-factor model; constants measured |
| P6 | `E\|A∩B\| = ab/m`; hypergeometric | **exact**, under the stated null model |
| P7 | `e^{−ab/(m−a−b+1)} ≤ Pr[∅] ≤ e^{−ab/m}` | upper bound **exact for all `(m,a,b)`**; lower bound **exact given `a+b ≤ m`**; the `≈ e^{−ab/m}` form is an **approximation with relative exponent error `O(d_A+d_B)`** |
| P8 | `E[κ_ρ]` per representation under Bernoulli(`d`) | **exact for all four**, given `w \| m` for `W` (`n−(n−1)(p₀²+p₁²)`). The `s`-parameterised column is **leading order** |
| P9 | complement symmetry: `R` `±1` with an exact sign rule; `W` exact; `S` asymmetric | **exact**, given both-fills compression *and* `w \| m` for `W` |
| Cor A | leading-order `E[κ_ρ]` monotone in `s`; U-shape under the null model | **exact**, *of the model, of the leading-order forms*. Constant-factor collapse holds **among `{S,C∘S,R,W}` only** — `B` is the exception and that is Theorem A |
| **Thm B** | `κ_R`, `κ_W` vary by `Θ(m)` at fixed density; `κ_B`, `κ_S` do not vary | **exact**, by explicit families; `κ_R` attains *every* integer in `[1, min(k,m−k+1)]` |
| P10 | `σ_R ≥ max(d, 1−d+1/m) ≥ ½`, unbounded above | **exact**, at every density |
| Cor B | no density-only selector can see `σ` | **exact** as an impossibility. It does **not** imply modelled selection is worse than no selection (measured, A6), and it does **not** select probe-and-commit over a model on a structural statistic |
| P11 | binned refinement never widens the interval | **exact**; the zone-map popcount is an **upper bound** on the count of non-degenerate bins, not equal to it |
| **P11b** | concentration: `E[K \| K≥1] = W_z d/p`, with closed-form conditional variance | **exact**; both moments verified against exhaustive enumeration. `Var → 0` as `d → 0` |
| **P11c** | composition `= c/W_z + p_Ap_B·κ(d/p)`; three regimes, window `x e^{−x} > c/w` in `x = W_z d` | **exact given the model**, and the model's hypotheses are strong: independence of `A`,`B`, Bernoulli within a bin, and the §5 null envelope. A **model of work**, not of time |
| P12 | headroom ratio falls below one at `s = ½` | **exact under the null model**; false for structured rows — side condition mandatory |
| **P13** | membership-probe model needs `≥ m/2` probes, independent of `s` | **exact**, derived in §9.1; included to show `U` is *not* the lower bound |
| — | clustering raises `Pr[disjoint]` above the uniform prediction | **heuristic** |

---

## 12. Errata against the `NARRATIVE.md` §1c sketch

Recorded so the narrative can be corrected in place.

1. **"Cost is monotone in `s`" is true of the null-model *expectations* and of `B` and `S`; it is
   false for `R` and `W` on arbitrary data** (Theorem B). Per the author's correction this is not a
   caveat but the contribution — the sentence should read that the *bound* and the *expected* costs
   are monotone in `s`, and that real representations deviate below that, without limit.
2. **The interval width has a cleaner general form** than the `a = b` case given:
   `min(a,b,m−a,m−b) = m·min(s_A,s_B)`. Worth stating, because it says one extreme side suffices.
3. **"The interval closes at rate `m(1−d)`"** confuses width with rate. Width is `m(1−d)`; the rate
   is the constant `m` per unit density.
4. **`Pr[∅] ≈ e^{−ab/m}` needs a condition.** It is an upper bound always, and accurate to relative
   exponent error `O(d_A+d_B)`. Carry the two-sided bound.
5. **"Information-theoretic floor" overclaims.** Nothing here lower-bounds algorithmic work. Use
   "metadata floor" / "identifiability floor" and name what it floors.
6. **"Expected intersection density falls quadratically"** is the equal-density statement of a
   bilinear quantity `d_A·d_B`. Fine to keep, worth one clause of precision.
7. **The pigeonhole factor is 2** — `51 % → 2 %`. `NARRATIVE.md` already states this correctly; the
   `1 %` form must not return.
8. **[GAP 1] is closed analytically, not experimentally.** `R`'s complement symmetry
   (Proposition 9) is why `R×R` wins the dense tail. The dense arm still needs a *designed* sweep
   with `C×B`/`C×C`, but it is no longer an unexplained accident.
9. **The unifying principle splits in two** (§7). "Work is proportional to residual uncertainty"
   covers Mechanism I only. Mechanism II — structure reducing traversal cost without narrowing the
   interval — is a separate claim and needs its own sentence.
10. **Claim A2 in the `NARRATIVE.md` §5 ledger is a casualty of Theorem B and must be restated.** It
    currently reads "Cost is monotone in effective sparsity `s = min(d,1−d)`, hence U-shaped and
    symmetric about `d = ½` **by construction**", as a claim about *measured* cost. Theorem B is a
    direct counterexample: at fixed density `κ_R` ranges over `[1,k]`, so measured cost is not a
    function of `s` at all. Suggested restatement, which loses nothing and gains the deviation:
    *`B×B`'s cost is flat; the envelope of the compressed cells is monotone in `s` under a null
    model; what is measured is the crossover in `s`, the magnitude of the win, and the deviation
    below the null.* This is the one live inconsistency between this document and the claim ledger.
    (`NARRATIVE.md` is not edited from here.)

---

## 13. Proposed display item — the figure that makes this legible

Written in the style of `DISPLAY_ITEMS.md` §§2–3. **Specification only; not built.**

### Recommendation in one line

**Merge this into Fig. 2 rather than spending a seventh slot.** Fig. 2 already owns the density
axis; what this adds is a *predicted* series against the *measured* one, which is a change of
content, not of subject. The six-item budget is unchanged. What it displaces is Fig. 2's current
panel (c), the winning-variant heat strip, which moves to Supplementary — Fig. 3a (the strategy map)
is already the paper's designated "which mechanism wins where" artefact and the two overlap.

Retitle the item: **"What the bound predicts, and where real data leaves it."**

---

### 13.1 Panels

Three panels, stacked, sharing the density axis between (b) and (c).

#### (a) The interval — analytic, no measured data

Carries Propositions 1–3 and Proposition 6.

- **x**: density `d`, **linear**, `[0, 1]`. Linear on purpose: this is the one panel where the
  pigeonhole geometry is the message, and a log axis destroys it. Equal densities `d_A = d_B = d`.
- **y**: `|A ∩ B| / m`, linear, `[0, 1]`.
- **Marks**:
  - Filled band between the floor `max(0, 2d−1)` and the ceiling `d`. Neutral grey `#BBBBBB` at low
    alpha — this is "what is not known", not a representation.
  - Floor and ceiling as solid ink lines, directly labelled `a+b−m` and `min(a,b)` at their
    right-hand ends. No legend.
  - Dashed curve `E|A∩B|/m = d²` inside the band (Proposition 6). Label it `E = d²` in place.
  - A second, thin curve on a right-hand axis: interval width `/m = s = min(d, 1−d)`. Triangle
    peaking at `(½, ½)`. Direct label "residual uncertainty".
  - Three vertical annotations only: `d = ½` (width maximal); the lift-off point of the floor, which
    is also `d = ½`, annotated once as "floor lifts off"; and a callout at `d = 0.51` reading
    "2 % of the universe must overlap".
- **The reading, stated in the caption**: the band is what cardinality leaves open; it pinches to
  nothing at both ends and is widest in the middle.

#### (b) Predicted cost per representation — analytic, no measured data

Carries Proposition 8 and Corollary A. This panel is what makes the U a theorem on the page.

- **x**: density, **mirrored log**. Left half `log d` from `10⁻⁶` to `½`; right half `log(1−d)`
  reversed, `½` to `1 − 10⁻⁶`. Tick labels give `d` on both halves. This is the correct axis for a
  quantity symmetric about `½` and it makes the U literally symmetric on the page; a plain log-`d`
  axis hides the dense arm and is the reason the dense tail has read as an afterthought.
  Equivalently, label the axis `s = min(d, 1−d)` with a mirrored top scale.
- **y**: expected descriptor items per universe position, `E[κ]/m`, log.
- **Series** (house palette, §1.1 of `DISPLAY_ITEMS.md`), all from closed forms in §5 above:

  | series | curve | colour | note |
  |---|---|---|---|
  | `B` | `1/w`, horizontal | `#222222` ink, heaviest line | the fixed cost; the reference in every panel |
  | `S` | `d` | `#4477AA` | monotone, not symmetric |
  | `C∘S` | `1−d` | `#CCBB44` | mirror of `S`; together they trace `s` |
  | `R` | `d(1−d) = s(1−s)` | `#228833` | the inverted U, peak `¼` at `d = ½` |
  | `W` | `(1/w)[1 − (1−d)^{2w} − d^{2w}]` | `#AA3377` | saturates onto the `B` line |
  | envelope | `min` of the above | ink, dotted, no marker | the U, drawn once |

- **Annotate the crossover** at `w·s ≈ 1` where the envelope leaves the `B` line — the analytic
  crossover, distinguished in the caption from the measured one in panel (c).
- Word width `w` is a stated parameter of the panel, printed in the axis furniture (`w = 64`).

#### (c) Predicted against measured — the headroom panel

Carries Theorem B, the definition of `σ`, and A2. This is the panel the campaign feeds.

- Same mirrored-log x-axis as (b), `sharex`.
- **y**: cost per pair, log, in the campaign's own units (ns/pair), with the analytic curves of (b)
  rescaled onto it by a single stated constant so that the `B×B` predicted and measured lines
  coincide at `d = ½`. **That rescaling must be stated in the caption as a single global constant,
  not fitted per series** — otherwise the panel is a fit, not a comparison.
- **Two families of line, and the second encoding must not collide with §1.1.** §1.1 already spends
  dash style on cell family, so predicted-versus-measured is encoded by **weight and alpha plus
  marker presence**: predicted curves are thin, 35 % alpha, **no markers**; measured curves are full
  weight, full alpha, with the §1.1 marker for their cell. Hue and dash keep their §1.1 meanings
  exactly.
- **Shade the vertical gap** between each measured curve and its own predicted curve wherever
  measured falls below predicted. That shading is the exploitable headroom, per cell, and it is the
  single thing a reader should take from the figure.
- Overlay real corpora as **points**, not lines: one marker per corpus at its median density and
  measured best-cell cost, in neutral grey with a leader line to its name for the three or four
  corpora that sit furthest below the prediction. Cap the labelled set — an unlabelled scatter is
  fine for the rest.

---

### 13.2 Data schema

Two series, and they must not share a file.

**Analytic (no campaign dependency; computable today).**

```
figures/gen/theory_curves.py  →  emits nothing to disk; curves computed inline
parameters:  m (universe, for axis furniture only), w = 64, V (annotation only),
             d grid: 400 points, geometric in s over [1e-6, 0.5], mirrored
functions:   kappa_B(d) = 1/w
             kappa_S(d) = d
             kappa_Cs(d) = 1 - d
             kappa_R(d)  = d*(1-d)
             kappa_W(d)  = (1/w)*(1 - (1-d)**(2*w) - d**(2*w))
             envelope(d) = min(kappa_B, min(kappa_S, kappa_Cs), kappa_R, kappa_W)
             interval_lo(d) = max(0, 2*d - 1)
             interval_hi(d) = d
             expected(d)    = d*d
             width(d)       = min(d, 1-d)
```

No provenance statement is needed beyond a docstring pointing at `THEORY.md` §§1, 5 — these are
closed forms, not measurements, and the caption must say so.

**Measured — density sweep (panel c lines).** Extends the existing sweep schema; one record per
`(host, cell, variant, density, spectrum, repeat)`:

| field | type | note |
|---|---|---|
| `host` | str | |
| `cell` | str | one of the ten symmetric cells, plus `CxB` |
| `variant` | str | implementation raced |
| `density` | float | both sides at matched density |
| `spectrum` | str | `uniform` \| `inverse` — both required, per the standing rule |
| `universe_bits` | int | pinned along the axis |
| `residency` | str | named regime, stated with every throughput number |
| `ns_per_pair` | float | |
| `repeat` | int | minimum over repeats is the reported statistic for synthetic sweeps |

**Measured — descriptor lengths (panel c corpus points, and the `σ` column of Table 1).** This is
the one new artifact the figure needs, and it is close to free: every representation is already
built, so the counts exist at build time and no timing is involved. One record per `(corpus, row)`,
or per corpus if aggregated:

| field | type | note |
|---|---|---|
| `corpus` | str | |
| `row_id` | int | omit if aggregating |
| `universe_bits` | int | `m` |
| `cardinality` | int | `k`; density is `k/m` |
| `n_runs` | int | `κ(R)` |
| `n_literal_words` | int | `ℓ` |
| `n_fill_groups` | int | `g`; `κ(W) = ℓ + g` |
| `sigma_R` | float | derived: `k(m−k+1)/m ÷ n_runs` |
| `sigma_W` | float | derived, against Eq. (5) of `THEORY.md` §5 |

Report `σ` per corpus as a **median with range**, following the same discipline as every other
per-corpus quantity (L6). It is a ratio of counts, so it is exactly reproducible and carries no
run-to-run dispersion of its own — say so, since it is then the most stable number in the paper.

---

### 13.3 Draft caption (T-P-D-S)

> **Fig. 2 | What the bound predicts, and where real data leaves it.**
> **a**, What operand cardinalities alone determine. For two sets of equal density `d` over `m`
> positions, `|A ∩ B|` is confined to the shaded band between `max(0, a+b−m)` and `min(a,b)`; the
> band's width is `m·min(d, 1−d)` (right axis), maximal at `d = ½` and vanishing at both extremes.
> Above `d = ½` the floor lifts off zero at twice the excess density — at `d = 0.51`, two per cent of
> the universe must overlap whatever the sets are. Dashed curve, the expected intersection density
> `d²` under a uniform null model. Analytic; no measured data.
> **b**, Expected number of stored items per universe position for each representation under a
> Bernoulli(`d`) null model (`w = \nm{machine word width}`), on an axis mirrored about `d = ½` so
> that symmetry under complementation reads directly. A dense bitmap is flat at `1/w`; a sorted array
> is linear in `d` and its complement view linear in `1−d`; a run container follows `d(1−d)`,
> vanishing at *both* tails and peaking at `d = ½`; a fill-compressed stream costs about twice the
> sorted array in the sparse tail and saturates onto the bitmap line at mid density, which is why it
> never reaches the envelope. Dotted envelope, the cheapest representation at each density. Under
> randomness the four compressed representations track distance from `d = ½` and none beats another
> by more than a constant; the bitmap is the exception, and the gap between it and the envelope is
> the headroom of Eq. (3). The U-shape is a property of this bound and of the bitmap's flatness, not
> of any method. Analytic; no measured data.
> **c**, Measured cost per pair against the same axis (thin unmarked curves, the predictions of
> **b** rescaled to these units by the single global constant \nm{scale constant relating predicted
> items to measured ns/pair}; heavy marked curves, measurement). Shading marks where measurement
> falls below prediction: real rows carry structure their density does not express, and that vertical
> gap is the headroom a representation can enter. Grey points, real corpora at their median density
> (\nm{count of corpora plotted}; labelled corpora are the \nm{count} furthest below prediction).
> Host \nm{host}, \nm{cache residency regime} throughout; each point the minimum of
> \nm{repeat count} repeats; oracle-checked, incorrect variants excluded. The `1/i` spectrum is in
> Supplementary \nm{item}.

---

### 13.4 Placement and budget

| item | before | after |
|---|---|---|
| Fig. 1 | schematic | unchanged |
| **Fig. 2** | density sweep (cost / speedup / winning-variant strip) | **predicted vs measured (interval / analytic curves / headroom)** |
| Fig. 3 | strategy map + residency | unchanged |
| Fig. 4 | cost of deciding | unchanged |
| Table 1 | real corpora | **add a `σ_R` column** (median [range]) — the per-corpus structure ratio |
| Table 2 | container census | unchanged |
| — | — | Fig. 2's winning-variant heat strip **→ Supplementary** |

Six main items, unchanged. Two notes on the consequences.

The `σ_R` column in Table 1 is the cheapest way to carry Theorem B onto the page per corpus without
a fourth panel, and it belongs beside the winning-cell column: a corpus with high `σ_R` should be won
by a run-based cell, and a reader can check that correspondence row by row. If it does not hold, that
is informative and should be reported, not smoothed.

Panel (b) is the one panel in the paper that can be built **today**, before any measurement lands.
Building it first is worth doing on its own: it fixes the mirrored-log axis, the palette assignment
and the envelope styling that panel (c) then reuses, and it is the panel a reader needs in order to
understand what panel (c) is a comparison against.
