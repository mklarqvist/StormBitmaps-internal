# Thresholded mode: an opt-in contract

Record of `RESEARCH_PLAN.md` §15.22–15.24 (claims C23–C24).
Harness: `bench/bench_threshold.cpp` · sweep: `bench/sweep_threshold.sh` ·
plot: `bench/plot_threshold.py` · data: [`threshold/sweep.csv`](threshold/sweep.csv) ·
figure: [`threshold/sweep.png`](threshold/sweep.png)

---

## 1. The contract

**Exact all-pairs remains the default and the paper's claim.** Some consumers
need every exact cardinality. Others do not — LD for plotting wants pairs above
an r² cutoff, and materialising the rest is wasted work. This is a *user-selected*
mode, not a replacement.

Correctness is preserved in the strong sense:

- reported cardinalities are **exact**, computed by the same kernels;
- only the *set* of reported pairs is restricted to those clearing the threshold;
- every run in the sweep is verified to produce a hit set **identical** to the
  exact scan's, and the sweep aborts on mismatch rather than dropping a row.

---

## 2. Why this unlocks a different literature

The applicable prior art is not the columnar/Parquet planner — that is largely
absorbed already (C23) — but **all-pairs similarity search**: Bayardo, Ma &
Srikant (WWW 2007); Xiao, Wang, Lin & Yu, ppjoin (WWW 2008); Chaudhuri, Ganti &
Kaushik (ICDE 2006). Its prunes all require a threshold, which is precisely why
they were unavailable under the exact contract.

Breadth pass, fraction of pairs eliminated before any intersection work at
t=0.01. No prune ever dropped a true hit.

| corpus | size | range | **prefix** | **bucket-cnt** | ANY | true hits |
|---|---:|---:|---:|---:|---:|---:|
| as-skitter | 1.5% | 17.0% | **99.4%** | 99.3% | 99.4% | 0.57% |
| com-LiveJournal | 0.5% | 12.9% | **100.0%** | 99.9% | 100.0% | 0.04% |
| soc-Pokec | 0.2% | 4.7% | **99.9%** | 99.9% | 100.0% | 0.04% |
| com-Orkut | 0.5% | 1.3% | **99.7%** | 99.9% | 99.9% | 0.05% |
| census1881 | 32.7% | 63.3% | **98.6%** | 99.6% | 100.0% | 0.01% |
| wiki-Talk | 8.0% | 6.4% | **95.7%** | 97.9% | 98.6% | 1.08% |
| dimension_003 | 9.0% | 99.6% | **100.0%** | 100.0% | 100.0% | 0.00% |
| weather_sept_85 | 25.3% | 0.6% | 34.9% | **83.1%** | 83.1% | 16.85% |

Alpha is **prefix filtering**, with the bucket-count bound complementary —
prefix collapses to 34.9% on weather_sept_85 where the count bound still gives
83.1%. **Size filtering, which the Parquet survey had flagged as the promising
one, is the weakest of the family by a wide margin.**

**The structural point.** Prefix filtering is *candidate generation*, not pair
filtering. Every other mechanism in this project enumerates N(N−1)/2 pairs and
rejects most of them; this never enumerates them at all. With elements under a
global order and prefix length p(X) = |X| − ⌈t|X|⌉ + 1, any pair with J(A,B) ≥ t
must share an element in their prefixes, so indexing prefixes alone is exact for
the thresholded question.

---

## 3. Results

Gated speedup at **t=0.001** — the conservative end, and the figure to quote,
since speedup rises with t across the whole sweep.

| corpus | gated speedup | gate | pairs above threshold |
|---|---:|---|---:|
| com-LiveJournal | **89.61×** | prefix | 0.0368% |
| soc-Pokec | **75.52×** | prefix | 0.0827% |
| com-Orkut | **44.49×** | prefix | 0.4442% |
| as-skitter | **41.68×** | prefix | 0.4013% |
| uscensus2000 | **29.31×** | prefix | 0.0000% |
| dimension_003 | **15.69×** | prefix | 0.0000% |
| wiki-Talk | **6.29×** | prefix | 2.7574% |
| wikileaks-noquotes | **3.78×** | prefix | 3.0754% |
| dimension_008 | 1.00× | exact | 60.85% |
| weather_sept_85 | 1.00× | exact | 38.81% |
| census-income | 1.00× | exact | 61.65% |
| census1881 | 0.97× | prefix | 0.3216% |
| census1881_srt | 0.91× | prefix | 0.2613% |

At t=0.5 the winners reach 292–722×.

**The hit-rate panel explains the shape better than the speedups do.** The
corpora that gain most are exactly those where almost nothing clears the
threshold (0.00–0.44% of pairs); the three that fall back to the exact scan are
those where 39–62% of pairs do. There is no work to skip when most of the answer
is wanted — the method's benefit is bounded by how much of the output the caller
is willing to discard.

---

## 4. Two implementation defects worth more than any algorithmic choice

| stage | as-skitter | com-LiveJournal | census1881 |
|---|---:|---:|---:|
| `unordered_map` posting index | 7.58× | 8.73× | 0.01× |
| CSR index, no hashing | 39.76× | 35.16× | 0.07× |
| dense ranks precomputed | **108.67×** | **223.31×** | **1.16×** |

The first version stored postings in `unordered_map<uint32_t, vector<uint32_t>>`
— roughly 1M distinct keys, each owning a heap vector. The second still probed a
`rank` hash map once per prefix element *inside the timed loop*, about 1M lookups
per repeat on census1881.

Together those cost **14–26×**, and they made prefix filtering look
asymptotically unsuited to long-row corpora when it was merely badly
implemented. **census1881 went from 0.01× to 1.16× on implementation alone.**
Stopping after either of the first two measurements would have recorded a false
negative and closed the line of work.

---

## 5. The gate, and where it differs from C16

Prefix filtering must be declined where it loses. The predictor is **index
traffic per pair** — the sum of prefix lengths divided by the pair count —
which separates cleanly:

- corpora that should use it: est **0.08–1.72**
- corpora that should bypass: est **22.8–5917**

an order-of-magnitude empty band. Two earlier cost models failed first: one
estimated only candidate count from a 128-row sample, the other weighted index
traffic against verification work incorrectly. Both let census1881 through at a
16× loss. Only a diagnostic that printed the raw terms identified index traffic
as dominant.

**Where this differs from the exact-mode gate.** C16 achieved worst case exactly
1.00× across 17 corpora. **Thresholded mode does not.** The threshold of 5.0 was
chosen to sit in the empty band and recovers a genuine win
(`wikileaks-noquotes`, 1.00× → 3.78×), but the two census1881 variants then land
at 0.97× and 0.91×. They occupy a marginal region (est 0.74–1.22) that measures
anywhere from 0.91× to 1.25× across runs, so the choice inside it is noise
rather than signal. **Worst case is ≈0.91×, not 1.00×** — recorded rather than
tuned away.

---

## 6. Limitations

1. **The headline depends on a parameter the caller sets.** Quote t=0.001
   (3.8–89.6×); the t=0.5 figures (292–722×) assume an aggressive cutoff.
2. **Not never-worse**, unlike exact mode — see §5.
3. **Index build is inside the timed region.** For a single-shot query it would
   not amortise; for repeated queries against a fixed corpus it would be built
   once, and the measured figures are therefore pessimistic in that setting.
4. **The gate boundary is fitted to 13 corpora** and untested near its edge,
   though the band it sits in is wide (1.72 → 22.8).
5. **Panel 1 curves are visibly jagged** — the run-to-run variance documented
   throughout this project. The trend is solid; individual points are not.
6. **One microarchitecture** (Apple M4).
7. **Prefix filtering is the only member of the family implemented.** The
   bucket-count bound measured 83.1% pruning on weather_sept_85 — the one corpus
   where prefix collapses — and is untested. Positional and suffix filtering
   (ppjoin) are likewise unexplored.
