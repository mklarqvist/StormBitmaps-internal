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

---

## 14. Which representation is optimal, and when — the container-threshold problem

**Why this section exists, and what it adds to §§1–13.** Sections 1–13 establish that headroom
exists (Thm A), that it is invisible to density (Thm B), and that the mechanisms must be selected
between rather than stacked (P11c). None of them says *how to choose*, and none of them prices a
choice against a stated objective. This section supplies the missing layer: it defines the decision
problem, derives the boundary of every pairing's region of optimality, derives the array-to-bitset
crossover as a **formula** rather than a constant, states in its strongest true form what is and is
not wrong with a fixed threshold, and says exactly when metadata settles the decision.

It is written against a concrete incumbent — CRoaring — because the incumbent's constants are the
cleanest available instance of the general result, and because every claim about it below is checked
against the vendored source rather than recalled. **CRoaring's threshold is the exact optimum of the
objective it was chosen for.** Nothing here says otherwise; §14.3 derives that optimality before it
derives anything else.

Chunked setting, fixed once. The universe `[0,m)` is partitioned into **chunks** of `M` bits
(CRoaring: `M = 2^16`), each chunk stored in its own representation. Within a chunk,
`n = M/w` words (CRoaring on a 64-bit host: `n = 1024`), `k` is the chunk cardinality, `r` the chunk
run count, `d = k/M` the **in-chunk** density. Array elements occupy `e` bits each (CRoaring:
`e = 16`, verified — `array_container_serialized_size_in_bytes(card) = card * sizeof(uint16_t)`,
`croaring_amalg/roaring.h:2621`).

---

### 14.1 The decision problem, stated precisely

#### Definition 14.1 (cost model)

For an operation `ω` and a representation pair `(ρ_A, ρ_B)`, the cell cost is

```
  C_ω(ρ_A, μ_A ; ρ_B, μ_B)  =  Σ_j  c_j · N_j(ω, ρ_A, μ_A, ρ_B, μ_B),
```

where `μ_X = (k_X, r_X, ℓ_X + g_X)` is the operand's metadata, the `N_j` are **counts of primitive
items touched**, and the `c_j > 0` are per-item constants. Five item classes are used below:

| class | constant | what it is |
|---|---|---|
| word AND-plus-popcount, vectorised | `c_w` | one 64-bit word of a bitset × bitset pass |
| membership probe into a bitset | `c_p` | index, load, shift, test |
| merge step | `c_m` | one advance of a two-pointer sorted merge |
| galloping step | `c_g` | one exponential-search probe |
| rank lookup | `c_ρ` | one `O(1)` rank query against a directory |

**Every result below is a statement about the `N_j`.** The `c_j` enter only through the three ratios

```
  θ_pw = c_p/c_w ,     θ_mw = c_m/c_w ,     θ_ρw = c_ρ/c_w ,
```

and every threshold derived below is a function of those ratios and of `(M, w, e)`. The ratios are
**measured, not derived** — this is the tier-3 boundary of `AGENTS.md`, and no number in this
section is quotable as a timing claim without it. Nothing here converts operations to time; §9.2's
guard rail applies verbatim, and **[GAP 12] applies with particular force to §14.6**, where the work
model and the measurement disagree in sign.

#### Definition 14.2 (the optimisation problem)

Given a corpus `𝒳` of sets partitioned into chunks, a **pairing distribution** `π` over the chunk
pairs actually evaluated, and an operation `ω`, choose an assignment `ρ : chunks → {S, B, R, W}`
minimising

```
  C(ρ)  =  Σ_{(x,y) ~ π}  C_ω( ρ(x), μ_x ; ρ(y), μ_y )        subject to   Σ_x bytes(ρ(x), μ_x) ≤ F.
```

This is what "optimal representation" means here, and every claim below is relative to it. Three
things are part of the problem instance and not of the container: the pairing distribution `π`, the
operation `ω`, and the byte budget `F`.

#### Proposition 14 (the query objective is not separable; the storage objective is)

`Σ_x bytes(ρ(x), μ_x)` is a sum of per-container terms, so its minimiser is obtained by minimising
each term independently. `C(ρ)` is a **quadratic** pseudo-Boolean function of the assignment: the
cost of a pair depends on both operands' representations, so no per-container rule minimises it in
general.

**Proof.** The storage claim is immediate. For the query claim it suffices to exhibit a pair
interaction, which Definition 14.1 supplies: `C_∩card(S,k_A;B,·) = c_p k_A` while
`C_∩card(S,k_A;S,k_B) = c_m(k_A+k_B)`, so the cost attributable to `x` depends on `ρ(y)`. ∎

**This, and not the value 4096, is the structural difference between the two objectives**, and it is
the honest headline of the whole section. A storage-optimal assignment can be computed one container
at a time with no knowledge of the workload. A query-optimal assignment cannot, because there is no
such thing as the cost of a container — only the cost of a pair.

---

### 14.2 What CRoaring's rules are, read off the source

#### Proposition 15 (all three CRoaring container decisions are `argmin` over serialized size)

Verified against `third_party/croaring_amalg/`:

| decision | rule as written | in the notation above | source |
|---|---|---|---|
| array ↔ bitset | bitset iff `k > DEFAULT_MAX_SIZE = 4096` | `e·k > M`, i.e. `2k > 8192` bytes | `roaring.h:2486`, `:2621`, `:3443` |
| array → run | run iff `2 + 4r < 2k` | `bytes(R) < bytes(S)` | `roaring.c:9929–9941` |
| bitset → run | run iff `2 + 4r < 8192` | `bytes(R) < bytes(B)` | `roaring.c:9963–9975` |

Each is a **two-way** comparison of serialized sizes, and `DEFAULT_MAX_SIZE = 4096` is exactly the
cardinality at which an array container and a bitset container occupy the same bytes. In general

```
  τ_storage  =  M / e            ( = 65536/16 = 4096 for CRoaring on any host ).
```

Note what `τ_storage` does **not** contain: `w`, any `c_j`, `π`, `ω`, or `F`. It is a function of the
format alone. That is the correct answer to the storage question, and it is why it is a compile-time
constant rather than a tuned one.

#### Corollary 15a (`convert_run_optimize` is not confluent)

Because each comparison is two-way and *which* two depends on the container's present type, the same
set can end in different representations depending on how it arrived. From `ARRAY` the result is
`RUN` iff `2+4r < 2k`; from `BITSET` it is `RUN` iff `2+4r < 8192`. The two disagree exactly on

```
  (k−1)/2  ≤  r  ≤  2047      and     k ≤ 4096.
```

**Worked instance.** `k = 3000`, `r = 1600`: array `6000` B, run `6402` B, bitset `8192` B. Arriving
as `ARRAY` the container stays `ARRAY` (6402 ≥ 6000); arriving as `BITSET` it becomes `RUN`
(6402 < 8192) — 402 bytes larger than the three-way `argmin`, for the same set.

**Consequence, and it explains a measured regression.** A three-way `argmin` over
`{bytes(S), bytes(B), bytes(R)}` is confluent, never larger than the present rule, and about ten
lines. The non-confluence is also the mechanism behind the regression recorded in the vendored
modification's own comment: lowering `DEFAULT_MAX_SIZE` *before* `run_optimize()` changes which
comparison a borderline container is subjected to and can flip it into `RUN`, measured at a 46 %
regression on `census1881` (`third_party/croaring_modified/roaring.c:16366–16401`). Running the
promotion **after** `run_optimize()` sidesteps the interaction, which is what the vendored change
does.

---

### 14.3 The crossover as a formula

#### Proposition 16 (the three array-versus-bitset crossovers for count-only AND)

Item counts, read off the kernels (`container_and_cardinality`, `croaring_amalg/roaring.h:6000`):
`B×B` is `n` word AND-popcounts unconditionally (no early exit — the NEON and AVX2 `justcard` loops
run the full `BITSET_CONTAINER_SIZE_IN_WORDS`, `roaring.c:8084`, `:7816`); `S×B` is `k_S` membership
probes (`array_bitset_container_intersection_cardinality`, `roaring.c:10710`, a loop of
`bitset_container_contains`); `S×S` is a two-pointer merge of `≤ k_A + k_B` steps, or a galloping
pass of `≤ k_min⌈log₂(k_max/k_min)⌉` steps when `k_max > 64 k_min` (`roaring.c:6992–7016`).

Then, for a container of cardinality `k` under count-only AND:

**(i) Against a bitset partner.** Promote iff `c_w n < c_p k`, i.e.

```
  k  >  τ_∩(B-partner)  =  (c_w/c_p) · (M/w)  =  n / θ_pw .
```

**(ii) Against an array partner of cardinality `k_Y` (balanced regime).** Promotion turns a
two-sided merge into a one-sided probe: `c_m(k + k_Y)  →  c_p k_Y`. Promote iff

```
  k  >  (θ_pm − 1) · k_Y ,        θ_pm = c_p/c_m .
```

In particular **if `c_p ≤ c_m` the promotion helps at every `k`** — a bitset is the cheapest thing to
be probed *into*, so making one side dense is free work-avoidance for the other side.

**(iii) Both sides promoted together, equal cardinality `k`.** Promote iff `c_w n < 2 c_m k`, i.e.

```
  k  >  τ_∩(symmetric)  =  n / (2 θ_mw) .
```

**Proof.** Each is the comparison of the two item counts named above; all are exact given
Definition 14.1. ∎

#### The general form, and the substitution

Putting Proposition 15 and Proposition 16(i) side by side gives the whole finding as one ratio:

```
  τ_storage  =  M/e ,        τ_∩  =  (c_w/c_p)·(M/w) ,
                                                              τ_storage      w
                                                             ─────────  =  ─── · θ_pw .
                                                               τ_∩          e
```

`w/e = 4` for CRoaring on a 64-bit host. **So the storage threshold exceeds the count-only-AND
threshold by exactly `4 θ_pw`**, and the two objectives disagree by that factor and no other. This
is the derived form of the finding; the constant `30–60×` quoted in `RESEARCH_PLAN.md` §26 is
`τ_storage/τ_∩`, and substituting the measured crossover reproduces it:

| substituted quantity | value | tier |
|---|---|---|
| measured symmetric crossover `τ_∩(symmetric)` | `k = 64–128` on the target host (C35) | **measured**, subject to re-measurement |
| implied `θ_mw = c_m/c_w` from (iii), `n = 1024` | `4 – 8` | derived from the above |
| implied `τ_storage/τ_∩(symmetric)` | `4096/64 = 64` to `4096/128 = 32` | derived |
| reported in `RESEARCH_PLAN.md` §26 | "30–60× off for this workload" | matches |

That the reported factor is recovered from `M/e` divided by a single measured crossover, with no
free parameters, is the consistency check this section owes.

> **Caveat, and it governs every substituted number in §14.** The campaign measured crossover
> **(iii)**, which fixes `θ_mw ∈ [4, 8]`. It did **not** measure crossover **(i)**, which is the one
> `τ_storage/τ_∩ = (w/e)θ_pw` is written in terms of. The two are related by
> `θ_pw = θ_mw · θ_pm`, and `θ_pm = c_p/c_m` — a bitmap probe against a merge step — is
> **unmeasured**. Where a numeric `θ_pw` is substituted below (`θ_pw = 16`), it assumes `θ_pm = 2`
> alongside the measured `θ_mw = 8`. That assumption is plausible (a probe is an index, a load, a
> shift and a test against a pointer advance and a compare) but it is **tier-1 until measured**, and
> every `θ_pw`-dependent number in §14.8 inherits its uncertainty linearly. Measuring (i) and (iii)
> together is one benchmark, it over-determines the model, and it would turn this assumption into a
> checkable prediction — `θ_pm` computed two ways must agree.

#### Theorem C (the two thresholds are the endpoints of one family, indexed by the price of a byte)

Introduce a byte price `λ ≥ 0` and minimise `C(ρ) + λ·bytes(ρ)`. For the `{S,B}` decision on a
container of cardinality `k` participating in `ν` pairs, write `Δw(k) ≥ 0` for the per-pair item
saving from promotion and `Δb(k) = M/8 − e k/8` for the byte cost. The Lagrangian rule is

```
  promote  ⟺  ν · Δw(k)  >  λ · Δb(k).
```

Then, since `Δw` is non-decreasing in `k` and `Δb` is strictly decreasing:

1. the rule is a **threshold rule** in `k` for every fixed `λ` and `ν`, with threshold `τ(λ, ν)`;
2. `τ(λ, ν)` is non-decreasing in `λ`;
3. `τ(0, ν) = τ_∩` and `τ(λ, ν) → τ_storage` as `λ → ∞`;
4. the range of `τ(·, ν)` is, up to integer rounding, exactly the interval `[τ_∩, τ_storage]`.

**Proof.** (1) `Δw(k)/Δb(k)` is non-decreasing in `k` on `k < τ_storage` (numerator non-decreasing,
denominator positive and strictly decreasing), so `{k : νΔw > λΔb}` is an up-set; for `k ≥ τ_storage`
we have `Δb ≤ 0 ≤ νΔw`, so those `k` are in the set for every `λ`. (2) The set shrinks pointwise as
`λ` grows. (3) At `λ = 0` the rule is `Δw(k) > 0`, which is Proposition 16; as `λ → ∞` only `Δb ≤ 0`
survives. (4) `τ(λ)` is the crossing point of two continuous functions of `k` and moves continuously
between the two endpoints. ∎

**Read this out loud, because it is the generous form of the finding.** `4096` and `64` are not a
right answer and a wrong answer. They are `τ(∞)` and `τ(0)` of the same one-parameter family, and
**every threshold between them is optimal for some price of a byte.** CRoaring's choice is the
unique threshold at which promotion is *never* paid for in bytes — the largest threshold at which
the compute-favourable move is footprint-neutral. That is a coherent and defensible objective for a
general-purpose library whose users' workloads it cannot see.

**One consequence worth isolating.** `τ(λ, ν)` depends on `ν`, the number of pairs the container
participates in. Two containers with identical `k` and different `ν` have different optima whenever
`λ > 0`. So under any byte budget the optimal rule is **not a function of the container at all**,
and no per-container rule — CRoaring's or ours — attains it.

---

### 14.4 Regions of optimality in the parameter space

Normalise every cost to units of one `B×B` chunk pass (`n c_w = 1`). With `k = d M = d w n`:

| cell | normalised cost | derivation |
|---|---|---|
| `B×B` | `1` | P4 |
| `S×B` | `θ_pw · w · d_A` | `c_p k_A / (n c_w)` |
| `S×S`, balanced | `θ_mw · w · (d_A + d_B)` | merge |
| `S×S`, skewed (`k_max > 64 k_min`) | `θ_gw · w · d_min · ⌈log₂(k_max/k_min)⌉` | galloping |
| `R×B`, no rank index | `d_A + r_A/n` | `Σ_i⌈len_i/w⌉ + r ≈ k_A/w + r_A` words; verified below |
| `R×B`, with rank index | `2 θ_ρw · r_A / n` | P5b |
| `R×R` | `θ_mw · (r_A + r_B)/n` | P5b corollary |

The `R×B`-without-rank row is not an assumption: `run_bitset_container_intersection_cardinality`
(`roaring.c:10901`) loops over runs calling `bitset_lenrange_cardinality` (`roaring.h:1780`), which
popcounts `⌈len/w⌉ + O(1)` words per run. Summing, `Σ_i ⌈len_i/w⌉ + r ≈ k/w + r` words.

The boundaries follow by equating pairs of rows. Writing them in the form a selector would evaluate:

```
  S×B  beats  B×B      ⟺   k_A  <  n/θ_pw                            ( = τ_∩ )
  S×S  beats  S×B      ⟺   k_B  <  (θ_pm − 1) k_A
  S×S  beats  B×B      ⟺   k_A + k_B  <  n/θ_mw
  R×B  beats  B×B      ⟺   r_A  <  n/(2θ_ρw)                          [rank index]
  R×B  beats  S×B      ⟺   r_A  <  k_A · θ_pw/(2θ_ρw)                 [rank index]
  R×R  beats  R×B      ⟺   r_A + r_B  <  2θ_ρw r_A/θ_mw
```

Four readings, each of which is a design rule rather than a curiosity.

1. **`τ_∩ = n/θ_pw` is the only boundary that involves the universe.** Every other boundary is a
   comparison between two data-proportional quantities. That is Proposition 4 reappearing: `B` is the
   sole representation indexed by the universe, so it is the sole source of a *fixed* threshold.
2. **`R×B` beats `S×B` iff the mean run length `L̄ = k_A/r_A` exceeds `2θ_ρw/θ_pw`.** A run
   container wins over a sorted array against a bitset partner as soon
   as runs are longer than a small machine-dependent constant, *independently of density*. That is
   the container-level statement of Theorem B.
3. **The rank index is not an unconditional improvement over `bitset_lenrange_cardinality`.**
   Comparing the two `R×B` rows, rank wins iff `2θ_ρw r < k/w·n/n + r`, i.e. iff the **mean run
   length** satisfies

   ```
     L̄  =  k/r   >   w · (2θ_ρw − 1).
   ```

   The asymptotic statement is nonetheless clean and is P5b: the present cost is `Θ(k/w + r)` and is
   therefore a function of run *length*, while a rank index makes it `Θ(r)` and independent of it.
   The gain factor is exactly `1 + L̄/w`, unbounded in `L̄`. `θ_ρw` is unmeasured and must be
   measured before any claim of benefit; the condition, not the benefit, is what is derived here.
4. **Skew is a separate axis from density.** The galloping row is the only one whose cost is
   sublinear in the larger operand, and its region — `k_max > 64 k_min` in CRoaring — is a region in
   the *ratio* of cardinalities, invisible to any per-container rule. A per-container decision cannot
   express it, which is Proposition 14 again.

---

### 14.5 Is a fixed threshold ever optimal?

This is the load-bearing result and the one easiest to state falsely. It is therefore stated first in
the direction that **weakens** the paper's rhetoric, because that version is true and the stronger
one is not.

#### Proposition 17 (a fixed threshold *is* optimal, under five hypotheses)

Assume

- **H1** the candidate set is `{S, B}` only;
- **H2** the operation is count-only AND;
- **H3** the constants `c_j` do not depend on the resident footprint;
- **H4** there is no byte budget (`λ = 0`);
- **H5** every container faces the same partner mix.

Then the optimal assignment is the fixed cardinality threshold `τ_∩` of Proposition 16, which is a
function of `(M, w)` and the machine constants only — **independent of the corpus and of the
pairing distribution.**

**Proof.** Under H1–H2 the cost of a pair is one of the four entries of Proposition 16. Under H5 the
promotion decision for a container is the same functional of `k` for every container; under H4 there
is no coupling through the budget; the decision is then the threshold comparison of Proposition 16,
which under H3 has workload-independent constants. ∎

**So "no fixed constant is optimal" is false as an unqualified statement.** Anyone asserting it must
drop at least one of H1–H5, and must say which. The rest of this subsection drops them one at a time
and shows each drop is real.

#### Theorem D (each hypothesis fails, and each failure has an exact witness)

**(a) Dropping H1 — run containers.** No rule that reads cardinality alone can implement `argmin`
over `{S, B, R}`. By **Theorem B**, at fixed `k` the run count attains *every* integer in
`[1, min(k, M−k+1)]`. The `R×B` cost is strictly increasing in `r` (both rows of §14.4) while the
`S×B` cost does not depend on `r`. Hence two containers of the same cardinality have different
optimal representations, and any `k`-only rule mis-ranks one of them. **Exact**; it is Corollary B
specialised from selectors-in-general to the container decision.

**(b) Dropping H5 — partner mix.** Let a container of cardinality `k` face bitset partners with
probability `β` and array partners of mean cardinality `k̄` otherwise. Promotion is favourable iff

```
  β c_p k + (1−β) c_m (k + k̄)   >   β c_w n + (1−β) c_p k̄ ,
```

so the optimal threshold is

```
                β c_w n  +  (1−β)(c_p − c_m) k̄
  τ*(β, k̄)  =  ────────────────────────────────  ,
                    β c_p  +  (1−β) c_m
```

which interpolates between `τ*(1, ·) = n/θ_pw = τ_∩` and `τ*(0, k̄) = (θ_pm − 1) k̄`. **`τ*` is a
statistic of the workload, not of the machine**: two corpora with the same containers and different
pairing distributions have different optima. **Exact**, given Definition 14.1.

*The fixed point, and why the decision is self-referential.* `β` is not exogenous — the partners are
containers subject to the same rule. Writing `β(τ)` for the pair-weighted fraction of partners with
`k > τ`, an optimal fixed threshold must satisfy `τ = τ*(β(τ), k̄(τ))`. Both sides are bounded and
`τ*` is continuous in `β`, so a fixed point exists on `[0, M]` by the intermediate value theorem
whenever `β` is continuous; it need not be unique. **A library's container threshold is therefore a
fixed point of its own workload, which is exactly why it cannot be a compile-time constant without
choosing an objective that removes the dependence — as the storage objective does.**

**(c) Dropping H4 — the byte budget.** Theorem C: the threshold is the dual variable of the
footprint constraint and sweeps `[τ_∩, τ_storage]`. **Exact.**

**(d) Dropping H2 — the operation.** Two facts, in this order.

*First, for cardinality-only queries the operation dimension collapses, and this is not an
approximation.* CRoaring computes `|A∪B|`, `|A∖B|` and `|A△B|` from `|A∩B|` by inclusion–exclusion
(`roaring.c:17900–17922`: `or_cardinality` returns `c1+c2−inter`, `andnot_cardinality` `c1−inter`,
`xor_cardinality` `c1+c2−2·inter`). So **all four cardinality queries have the same cost matrix**,
and no operation-dependence exists for them. This closes, for the container decision, the part of
[GAP 11] that concerns cardinality: the map cannot differ by operation when three of the operations
are the fourth plus two subtractions.

*Second, for **materialising** operations the matrix differs in sign.* A materialising union with a
bitset operand must produce a bitset-sized result: `B×B` union costs `n` words, and `S×B` union costs
`n` words *plus* `k_S` insertions. Hence against a bitset partner, promotion **always** helps for
materialising union (it removes the `c_p k` term at no cost), whereas for count-only AND it helps
only above `τ_∩ > 0`. The two thresholds are therefore `0` and `τ_∩`, and **no single stored
assignment is optimal for a workload mixing the two** unless one dominates. **Exact**, given the
item counts.

**(e) Dropping H3 — residency.** `c_w` is not a constant: a bitset chunk is `M/8` bytes and an array
chunk is `ek/8`, so promoting a container multiplies its footprint by

```
  M / (e k)   =   τ_storage / k ,
```

which is `64×` at `k = 64` and `1×` at `k = τ_storage`. **This is the second reading of
`τ_storage`, and it is the important one:** `τ_storage` is the unique threshold at which promotion
never inflates the working set. Every lower threshold buys items with bytes at an exchange rate of
`τ_storage/k − 1`, and the bytes are paid in `c_w`, which rises as the corpus leaves cache. This is
the **only** one of the five hypotheses whose failure is not analytic, and it is the one the
measurements say dominates — see §14.6.

#### How far a fixed choice can be from optimal

Bounded exactly, under H1–H3 and H5, against the family `{τ(λ)}` of Theorem C. A container at
cardinality `k` paired with bitsets costs `min(c_p k, c_w n)` at the optimum and `c_p k` or `c_w n`
under a fixed `τ`. The worst case sits at the far endpoint:

```
  max_k  C(τ_storage; k) / C(τ_∩; k)   =   c_p τ_storage / (c_w n)   =   τ_storage / τ_∩   =   (w/e)·θ_pw ,
```

attained at `k = τ_storage`; and symmetrically, running at `τ_∩` inflates the footprint of a
`k = τ_∩` container by `τ_storage/τ_∩`. **The exchange rate is the same number in both directions.**
Substituting the measured *symmetric* crossover (`k = 64–128`, so reading `τ_∩` as `τ_∩(symmetric)`
— the two coincide only under the `θ_pm = 2` assumption of §14.3) it is `32–64×`, which is the
`30–60×` of `RESEARCH_PLAN.md` §26, derived.

**The honest summary of §14.5, in one sentence.** A fixed threshold is optimal for a fixed objective
on a homogeneous workload with two candidate representations; the incumbent's constant is exactly
that optimum for the storage objective; and every one of the five hypotheses that makes the
statement true is violated by the workloads either project cares about, each with an exact witness
above.

---

### 14.6 What the work model does not explain, and this is a finding

Under Definition 14.1, promoting every container above `τ = τ_∩(symmetric) = n/(2θ_mw)` is **weakly
beneficial on every pair it affects**, provided `θ_pm ≤ 2`:

- both sides promoted (`k, k' > τ`): before `c_m(k+k')`, after `c_w n = 2 c_m τ`, and `k+k' > 2τ`;
- one side promoted (`k > τ`, partner an array with `k_Y ≤ τ`): before `c_m(k+k_Y)`, after
  `c_p k_Y = θ_pm c_m k_Y`, and `(θ_pm − 1) k_Y ≤ k_Y ≤ τ < k`;
- neither promoted: unchanged.

The work model therefore predicts that lowering the threshold from `τ_storage` to `τ_∩` never loses.
(The condition `θ_pm ≤ 2` is the assumption of §14.3's caveat, and it is exactly what makes the
second case go through — a stronger `θ_pm` would leave a band `τ < k < (θ_pm−1)k_Y` where the work
model itself predicts a loss. That band is not where the measured losses are, so it is not the
explanation.)

**It loses.** C35 measures `0.8×` on `uscensus2000` and `0.6×` on `as-skitter` — sparse corpora —
against `19.5×` on `census1881`. The sign flip is not in the item counts.

The mechanism is Theorem D(e). On a sparse corpus most containers sit just above `τ_∩`, so promotion
multiplies their footprint by `τ_storage/k ≈ 30–60×` while removing only a handful of merge steps
each; the corpus leaves cache and `c_w` rises for every bitset pair, including the ones promotion did
not touch. This is **the same divergence as [GAP 12]**, in a second and independent instance: an
analytic model of work, correct as far as it goes, mis-signs a decision because the penalty lives in
the memory system. Recording it here rather than eliding it, because the pattern is now twice
observed and is itself a result:

> **A work model can price a representation choice but cannot price its footprint, and footprint
> re-enters the work model as a multiplier on the very constant the choice was evaluated with.**

The constructive consequence is Theorem C read backwards: the byte price `λ` is not an accounting
fiction, it is the mechanism by which residency enters, and `λ` should be **fitted per host and per
corpus** rather than set to `0` or `∞`. The two constants in the field today are the two limits.

---

### 14.7 When the decision is settled by metadata, and when it provably is not

Two identifiability questions are easy to conflate and must not be. §1 asks what metadata determines
about the **answer**; this subsection asks what it determines about the **decision**. They have
different answers and the difference is favourable.

#### Proposition 18 (determinability of the decision)

Let `μ(X)` be the metadata held about `X` and say the decision is **determinable from `μ`** if
`argmin` over the candidate cells is a function of `(μ_A, μ_B)` alone.

1. **If `μ = (k, r, ℓ+g)` the decision is determinable exactly, in `O(1)`, touching no operand
   data.** Every cost in §14.4 is a closed-form function of `(μ_A, μ_B, n, w)` and the constants, so
   the `argmin` is too. This is the formal content of "selection must be near-free", and it is a
   consequence of the cost model rather than an engineering aspiration.
2. **If `μ = (k) alone the decision is not determinable** whenever the candidate set contains `R`.
   Theorem D(a) is the witness. Adding one integer per container — the run count — restores
   determinability; adding cardinality precision does not.

#### Proposition 19 (the robustness band: the decision is either determined or immaterial)

Suppose each constant `c_j` is known only up to a multiplicative factor `√γ` in each direction, so
that any modelled cost is known within a factor `γ ≥ 1`. Order the candidate cells by modelled cost,
`C₁ ≤ C₂ ≤ …`, and define the **margin** `Λ = C₂/C₁`. Then:

- if `Λ > γ`, the true ordering of the two best cells is determined by `μ`, and the selector's choice
  is correct;
- if `Λ ≤ γ`, the ordering is not determined by `μ` — **and the cost of choosing wrong is at most
  `γ`, by the definition of `Λ`.**

**Proof.** If `Λ > γ` then even the most adverse admissible perturbation of the constants leaves
`C₁ < C₂`. If `Λ ≤ γ` then `C₂ ≤ γ C₁`, and the second-best cell costs at most `γ` times the best. ∎

**So for every pair, either the metadata settles the decision or the decision does not matter to
within the precision of the constants.** The undetermined set is a `γ`-neighbourhood of the
boundaries of §14.4 — the **mid-band of the decision** — and it is not a region where a selector
fails; it is a region where it cannot lose by more than `γ`.

**How this connects to §1, and the connection is the point.** Proposition 2 says the *answer* is
pinned by cardinality only at the density extremes, leaving `m·s` values open in the middle.
Proposition 19 says the *decision* is pinned everywhere except a `γ`-neighbourhood of a boundary,
and that the exception is bounded loss rather than unbounded error. **The mid-band of the answer and
the mid-band of the decision are different regions with different consequences**, and the paper must
not let a reader merge them: at `d = ½` cardinality determines nothing about the answer while
determining the decision completely — `B×B`, by Proposition 12.

**The caveat that makes `γ` interesting rather than cosmetic.** `γ` is the uncertainty in the
*model*, not in the constants alone. §14.6 and [GAP 12] show the work model and measured time
diverge by more than three orders of magnitude in at least one regime, so on the residency axis `γ`
is not small. **That is the derived motivation for probe-and-commit**: not that models are bad, but
that `γ` is large exactly where the model omits the memory system, and measurement is the only way
to shrink it. This is a narrower and more defensible statement than "the theory implies
probe-and-commit" (which Corollary B's guard rail forbids) — it says only that `γ` is large in a
named regime, and that a mechanism which reduces `γ` is worth its cost there.

---

### 14.8 Why an advantage survives repairing the incumbent's kernel

The finding of C35 removes a kernel-level advantage. The claim that replaces it is that the
avoidance layer still wins over the *repaired* baseline. That claim needs a proof of orthogonality,
not a measurement, because the obvious objection — "then put the avoidance layer in Roaring too" —
is correct and should be met by offering exactly that (`ROARING_PROPOSALS.md`), not by denying it.

#### Proposition 20 (exact decomposition, and the invariance)

Write the total cost of a batch of pairwise queries as

```
  C(ρ)  =  C_meta  +  Σ_{(A,B) ∈ 𝒫_surv}  Σ_{z ∈ Z(A,B)}  C_cell( ρ(A|z), ρ(B|z) ) ,
```

where `𝒫_surv ⊆ 𝒫` are the pairs not discarded by metadata and `Z(A,B)` the regions actually
visited. Then:

1. **`𝒫_surv` and `Z(A,B)` are functions of the metadata and the data only, and are invariant under
   every change to `ρ`.** The size bound reads `(a,b)`; the zone-map test reads bin occupancy; the
   thresholded mode reads `(a,b,t)`. None reads a representation, and representations are exact and
   interconvertible (§0), so changing `ρ` changes no membership.
2. Consequently, for a **uniform** kernel improvement by a factor `α > 1` (every `C_cell` divided by
   `α`),

   ```
                       C_base/α                         C_base
     G(α)  =  ────────────────────────────  =  ──────────────────────────  ,
              C_meta + N_surv Ē_surv / α        α C_meta + N_surv Ē_surv
   ```

   where `C_base = N_all · Ē_all` is the same batch with no avoidance. `G` is **invariant in `α`
   exactly when `C_meta = 0`, and decreases in `α` only through `C_meta`'s share of the
   post-avoidance cost.**

**Proof.** (1) is the statement that the three tests are functions of quantities `ρ` does not enter.
(2) is substitution. ∎

**This is the proof the claim needs, and it also states its own limit.** Improving the kernel cannot
subsume avoidance, because avoidance changes *which* work is asked for and the kernel changes only
what the asked-for work costs. But the advantage is not perfectly invariant: a faster kernel raises
the relative weight of the summary, so `G` erodes toward `C_base/(α C_meta)`. **The erosion is
governed by the summary's share of the post-avoidance cost**, which is precisely what
Proposition 11c's window measures — and it predicts the sign of the C35 result: on sparse corpora
where the summary dominates, repairing the kernel erodes the advantage most.

#### Proposition 21 (which savings multiply and which do not)

By the composition rule (`NARRATIVE.md` §2, and `theory_block.tex` Eq. `shortfall`): mechanisms
multiply when they act on different waste **and read different operand statistics**.

- **The zone map multiplies with container choice.** It reads bin occupancy; container choice reads
  cardinality and run count. Different statistics, different waste. ✔
- **The size bound does *not* multiply with container choice.** Both read cardinality, and in
  opposite directions: the size bound's survivors are the pairs with `a ≈ b`, while every cell in
  §14.4 is cheapest when `a` and `b` are *dissimilar* (the `S×B` and galloping rows are functions of
  `min(a,b)` against a fixed `B×B`). The composed cost exceeds the product by
  `E[min(a,b) | survivor] / E[min(a,b)] → ln K / 2`. ✘ — **and this is a new instance of the
  falsified-composition rule, not a restatement: it says a paper reporting a container-threshold gain
  and a size-bound gain must not multiply them.**

#### The shape of the surviving advantage, and what it predicts

Under the null model of §5, working **inside a chunk both operands occupy** (so the incumbent is
credited its chunk-key skip in full), with the incumbent using the compute-optimal container
threshold of Proposition 16 — i.e. the *repaired* baseline — and the challenger adding a zone map of
bin width `W_z` at overhead `c/W_z`:

```
                     κ_Ro(d)                                    θ_pw w d ,  d < 1/(θ_pw w)
  G(d)  =  ───────────────────────────  ,      κ_Ro(d) = min(1, θ_pw w d) = {
            c/W_z  +  p_A p_B · κ(d/p)                                        1 ,        otherwise
```

with `p = 1 − (1−d)^{W_z}` (Proposition 11b) and `κ` the §5 envelope.

**Two exact consequences, and the second is a design rule that did not exist before.**

1. In the sparse band `κ(d/p) ≡ 1`, because `d/p → 1/W_z` and a bin holding one bit in `W_z` is
   *above* the compute crossover whenever `W_z ≤ θ_pw w`. Substituting `p ≈ x` for `x = W_z d ≪ 1`,

   ```
     G(x)  =  θ_pw w x / ( c + W_z (1 − e^{−x})² ) ,
   ```

   maximised at `x* = √(c/W_z)`, giving

   ```
                    θ_pw · w
     G_max  =  ─────────────────  ,      at   d*  =  √c / W_z^{3/2} ,
                2 √( c · W_z )
   ```

   **valid on `(θ_pw w √c)^{2/3} ≤ W_z ≤ θ_pw w`**, and both edges bind. Below the lower edge
   `d*` would exceed `1/(θ_pw w)`, where `κ_Ro` saturates at `1` and the peak is capped. Above the
   upper edge `κ(d/p) < 1` — the concentrated content of an occupied bin falls *below* the compute
   crossover — and the peak saturates at the `W_z`-independent value `√(θ_pw w / c)/2`. At
   `θ_pw = 16`, `w = 64`, `c = 2` the valid window is `128 ≤ W_z ≤ 1024` and the saturated value is
   `11.3×`. *An earlier draft of this paragraph quoted `5.7×` at `W_z = 4096` by applying the closed
   form outside its range; the correct value there is `11.3×`.*

2. **`W_z* = θ_pw · w` is the bin width at which a singly-occupied bin sits exactly at the
   array/bitset compute crossover.** Above it the summary hands the kernel bins it should enumerate;
   below it, bins it should popcount. The zone-map granularity and the container threshold are
   therefore governed by the *same* measured ratio, which is a link neither §7 nor §14.3 had.

**Substituting.** With `θ_pw = 16` — the measured `θ_mw = 8` times the **assumed** `θ_pm = 2`, per
the caveat in §14.3, so every number in this table scales linearly with an unmeasured constant —
`w = 64`, `c = 2`:

| `W_z` | `G_max`, closed form | `G_max`, numeric scan | at in-chunk density `d*` | note |
|---|---:|---:|---:|---|
| 64 | — (below range) | `28.6×` | `9.8 × 10⁻⁴` | `κ_Ro` saturated; peak is capped, not given by the formula |
| 128 | `32.0×` | `34.0×` | `9.8 × 10⁻⁴` | lower edge of validity; `d*` sits exactly at the crossover |
| 256 | `22.6×` | `23.7×` | `3.8 × 10⁻⁴` | |
| 512 | `16.0×` | `16.5×` | `1.3 × 10⁻⁴` | the configuration the figures use |
| 1024 | `11.3×` | `11.6×` | `4.5 × 10⁻⁵` | upper edge, `W_z = θ_pw w` |
| ≥ 1024 | `11.3×` (saturated) | `11.4×` | — | `κ(d/p) < 1`; formula no longer applies |

The numeric column is a direct scan of `G(d)` on a `2 × 10⁵`-point geometric grid over
`d ∈ [10⁻⁸, ½]`, with no approximation of `p`; the closed form agrees within `4 %` across its range.
At `W_z = 512` the band over which `G ≥ 5×` is `d ∈ [2.0 × 10⁻⁵, 1.1 × 10⁻³]`, and `G > 1` exactly on
`d ∈ [3.8 × 10⁻⁶, 1.2 × 10⁻²]`. The lower edge is Proposition 11c's `d_low` with the per-item
constants carried:

```
  d_low  =  c / (θ_pw · w · W_z)   =  3.81 × 10⁻⁶      ( matched to three figures by the scan ).
```

P11c's tabulated `d_low = c/(w W_z)` is the `θ_pw = 1` case — it normalised a sorted array at one
word per element. Charging a probe its measured cost moves the window down by `θ_pw`, and the upper
edge from `x ≈ 5` to `x ≈ 6.2` in `x = W_z d`. **Nothing structural changes; the window is the same
window with the constants in it**, which is what §14 adds to §7 throughout.

**What this predicts, said plainly, including where it disagrees.** On **structureless** data the
derivation caps the exact-mode advantage over a repaired incumbent at about `32×`, attained at
`W_z ≈ 128`, and puts the `≥5×` band inside two decades of in-chunk density. A measured range of
`5–50×` is therefore **consistent at its lower and middle range and exceeds the structureless
ceiling at its top**, and the theory already says which way that must go: Proposition 10 makes the
deviation one-sided, `σ_R ≥ ½` and unbounded above, so real corpora can only be cheaper than the
null at the same density. **The excess above `32×` is attributable to structure and must be reported
as such**, not as agreement. Two further caveats bound the comparison: `κ_Ro` here is a two-cell
envelope and under-credits an incumbent whose run containers are working; and the thresholded mode
(rung 3) is excluded entirely — Eq. `survive` supplies `6.9×` to `90×` on its own, and by
Proposition 21 that factor **must not be multiplied** into the numbers above.

---

### 14.9 Result ledger for §14 (extends §11)

| # | Result | Status |
|---|---|---|
| D14.1 | cost model: items × measured constants; three ratios `θ_pw, θ_mw, θ_ρw` | **definition**; constants are tier-3, unmeasured except as noted |
| D14.2 | the decision problem: `argmin` over assignments, under `π`, `ω`, `F` | **definition** |
| P14 | storage objective is separable; query objective is quadratic in the assignment | **exact** |
| P15 | all three CRoaring container rules are `argmin` over serialized bytes; `τ_storage = M/e` | **exact**, verified against `croaring_amalg` source with line references |
| Cor 15a | `convert_run_optimize` is not confluent; disagreement region `(k−1)/2 ≤ r ≤ 2047`, `k ≤ 4096` | **exact**, by the two inequalities in the source; worked instance `k=3000, r=1600` |
| P16 | three crossovers: `n/θ_pw`, `(θ_pm−1)k_Y`, `n/(2θ_mw)` | **exact** given D14.1 |
| — | `τ_storage/τ_∩ = (w/e)·θ_pw`; substitution reproduces the reported `30–60×` | **derived**, from one measured crossover, no free parameters |
| **Thm C** | `τ(λ)` sweeps `[τ_∩, τ_storage]`; the two constants are the two limits | **exact**, given monotone `Δw`, strictly decreasing `Δb` |
| — | `τ(λ,ν)` depends on the pair count `ν`, so no per-container rule is optimal under a budget | **exact** |
| §14.4 | region boundaries for `S×B`, `S×S`, `R×B`, `R×R` | **exact** given D14.1; `θ_ρw` unmeasured, so the `R×B` rank condition is a condition, not a claim |
| **P17** | a fixed threshold **is** optimal under H1–H5, and equals `τ_∩` | **exact** — and it is the reason the negative result must be stated carefully |
| **Thm D(a)** | dropping `R` in: no `k`-only rule can rank `R` against `S` | **exact**, from Theorem B |
| **Thm D(b)** | `τ*(β,k̄)`; workload-dependent; fixed point exists, may not be unique | **exact** for `τ*`; the fixed-point existence needs `β` continuous |
| **Thm D(c)** | byte budget ⇒ Theorem C | **exact** |
| **Thm D(d)** | cardinality queries: **no** operation dependence (inclusion–exclusion, verified in source). Materialising ops: thresholds `0` vs `τ_∩`, opposite signs | **exact** |
| **Thm D(e)** | footprint multiplier `τ_storage/k`; `τ_storage` is the unique footprint-neutral threshold | **exact** as arithmetic; its *consequence* for `c_w` is **not** analytic — [GAP 12] |
| — | worst-case regret of a fixed threshold `= τ_storage/τ_∩ = (w/e)θ_pw`, both directions | **exact** under H1–H3, H5 |
| §14.6 | the work model mis-signs the sparse-corpus case; footprint re-enters through `c_w` | **finding**, not a theorem; measured evidence is C35, analytic explanation is D(e) |
| **P18** | determinable from `(k, r, ℓ+g)` in `O(1)`; not determinable from `k` alone with `R` present | **exact** |
| **P19** | margin `Λ > γ` ⇒ determined; `Λ ≤ γ` ⇒ loss bounded by `γ` | **exact**, and it is a statement about the *model*, so `γ` must absorb [GAP 12] |
| **P20** | `𝒫_surv`, `Z` are `ρ`-invariant; `G(α) = C_base/(αC_meta + N_survĒ_surv)` | **exact** |
| **P21** | zone map × container choice multiplies; size bound × container choice does not | **exact** given the composition rule; the shortfall factor is `ln K/2` |
| — | `G_max = θ_pw w / (2√(cW_z))` at `d* = √c/W_z^{3/2}`; `W_z* = θ_pw w` | **exact given the null model**; a model of **work**; substituted values depend on one measured crossover |

**Three things §14 does not establish, recorded here so the prose cannot drift into them.**

1. **No lower bound on algorithmic work appears anywhere in §14.** Every comparison is between
   achievable costs, as in §9.1. `τ_∩` is not a floor; it is where two achieved costs cross.
2. **No timing claim follows.** `θ_pw`, `θ_mw`, `θ_ρw` are measured ratios and only one of the three
   has been measured even indirectly. Every substituted number above is conditional on that
   measurement and is marked as such.
3. **§14.8's `G` is a null-model prediction of work.** It is a floor on the mechanism's value
   (Proposition 10 makes the deviation one-sided) and not a prediction of any measurement. Where it
   disagrees with the measured range, §14.8 says so rather than reconciling.
