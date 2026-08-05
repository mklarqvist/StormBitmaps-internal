# Tier-0 filter: making the zone map generically useful

Reproduce: `bench/gate_eval.sh < corpora.txt` (medians over independent
processes), single runs via `build_portable/bench_bloom`.
Raw: `results/filter/*.txt`, gate table `results/filter/GATE.md`.

## The problem

C14/C15 produced a coarse 2 kB zone map worth **3.5–4x** on sparse graph
corpora — and **0.39x on weather_sept_85, 0.45x on census-income**. A structure
that more than halves throughput on a third of the corpora is not deployable,
however good its best case. The goal here was a filter that is *generically*
good, not one tuned to the datasets it wins on.

## Result

| | always-on | **gated** |
|---|---:|---:|
| geometric mean over 17 corpora | 1.345x | **1.554x** |
| worst case | **0.39x** | **1.00x** |
| corpora harmed (<0.98x) | 6 | **0** |

Gating both *raises* the mean and removes every regression. The filter is
selected on 9 of 17 corpora.

| corpus | survival | touch | gate | always | **gated** |
|---|---:|---:|---|---:|---:|
| com-LiveJournal | 0.010 | 0.002 | use | 3.93 | **3.93** |
| com-Orkut | 0.011 | 0.007 | use | 3.67 | **3.67** |
| soc-Pokec | 0.007 | 0.005 | use | 3.39 | **3.39** |
| as-skitter | 0.050 | 0.002 | use | 2.48 | **2.48** |
| census1881_srt | 0.009 | 0.006 | use | 2.30 | **2.30** |
| uscensus2000 | 0.019 | 0.000 | use | 2.17 | **2.17** |
| wikileaks-noquotes_srt | 0.002 | 0.163 | use | 1.81 | **1.81** |
| dimension_003 | 0.000 | 0.002 | use | 1.46 | **1.46** |
| wikileaks-noquotes | 0.131 | 0.213 | use | 1.12 | **1.12** |
| dimension_008 | 0.933 | 0.002 | bypass | 1.15 | 1.00 |
| wiki-Talk | 0.274 | 0.002 | bypass | 1.15 | 1.00 |
| dimension_033 | 0.063 | 2.848 | bypass | 0.92 | **1.00** |
| census1881 | 0.983 | 0.024 | bypass | 0.88 | **1.00** |
| weather_sept_85_srt | 0.620 | 12.05 | bypass | 0.74 | **1.00** |
| census-income_srt | 0.389 | 4.531 | bypass | 0.62 | **1.00** |
| census-income | 0.999 | 12.30 | bypass | 0.45 | **1.00** |
| weather_sept_85 | 0.971 | 7.351 | bypass | 0.39 | **1.00** |

## The gate

Two independent failure modes, both of which must be tested:

**Selectivity.** If most probes survive the filter it is pure added work.
*Fill rate does not predict this* — `dimension_008` has fill 0.0002 and survival
0.933, because the sparse side's elements land precisely in the dense side's
occupied buckets. Correlated data defeats any static estimate, so survival must
be **measured**, not derived.

**Access density.** Even a selective filter loses when |S| is large enough that
the bitmap probes stop being random. `dimension_033` has survival 0.063 but
touches 2.85 cache lines per line of bitmap, which the hardware prefetcher
already streams; a filter can only recover a miss that would have been taken.

Both are settled by sampling pairs and committing — the probe-and-commit the
selector already performs per tile, so this is an extra column in an existing
decision rather than a new mechanism.

```
use_filter  <=>  survival < 0.15  AND  touch < 0.25
```

**These are plateau centres, not fitted points.** Every combination with
`survival in [0.10, 0.25]` and `touch in [0.20, 0.50]` gives geomean
1.503–1.508 at worst case 1.00. The flatness across a 2.5x range in each
parameter is the evidence that the gate is not tuned to individual datasets.

**Gate cost**, measured over 2000 iterations (a 32-iteration timing read 0 ns,
below the ~41 ns clock granularity, which is not evidence of being free):
1,427–50,350 ns per tile, i.e. **0.07–2.53 ns/pair amortised = 0.08–1.2% of
runtime** — inside the 2% selection budget of Gate 1.

## Sampling correctness

Two bugs found and fixed while building this, both previously seen in this
project:

1. The gate first sampled `pairs[0..16]`, which in a strided upper-triangle walk
   all share `i=0` — one row's selectivity reported as the corpus's. It read
   as-skitter at survival 0.234 against a true 0.022 and **bypassed a 3x win**.
   Same truncation-vs-striding error the pair sampler itself once had.
2. The sample was then sized in *pairs*; at |S|=6 that is ~190 probes, far too
   few. It is now sized in **probes** (>=4096), strided across the pair list.

## What did not work

- **Hashed Bloom filter** (C14): loses to a size-matched positional map on 9/10.
- **Blocked Bloom**: worse than plain on 10/10.
- **Adaptive width proportional to |A|** (C15): loses on 7/9 — survival, not
  footprint, is what matters while both fit cache.
- **Group-collapsing consecutive same-bucket probes** (C15): 0.13x vs 0.41x on
  weather; a serial dependent loop with a per-element branch loses to branchless
  probing that does strictly more lookups.

## Limitations

- One microarchitecture (Apple M4).
- `dimension_008` and `wiki-Talk` forgo ~1.15x by bypassing, but both measured
  0.81x and 0.52x on earlier runs — they sit inside the noise band, and the gate
  is deliberately conservative there.
- as-skitter's measured survival varies 0.050–0.137 between runs against a 0.15
  threshold; it has always landed on "use", but it is the closest to flipping.
- Filter *build* cost is still not charged: filters are constructed before
  timing, as with `build_occ`.
