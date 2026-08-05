# Evidence checklist — every number the manuscript needs

Auto-extracted from `sections/*.tex`. Each entry is one `\nm{}` slot in the draft.
Regenerate with `python3 tools_extract_slots.py` after any edit.
**167 slots total.**

## abstract (3)

1. count of asymmetric measurement points tested
2. count of real corpora tested
3. selection cost as percent of runtime under the hoisted selection policy

## introduction (4)

1. count of representation-pairing cells evaluated
2. count of microarchitectures tested
3. count of real corpora tested
4. count of asymmetric measurement points tested

## results_a (46)

1. count of pairwise kernels drawn in the pairing-matrix schematic, ten or eleven depending on the resolution of the C$\times$B reconciliation, Methods
2. count of pairing cells swept in the density sweep
3. universe size held fixed across the density sweep, in bits
4. cache level the density sweep is resident in, e.g.\ L2
5. Supplementary item number carrying the 1/i density sweep
6. $\cell{\rB}{\rB}$ cost, ns per pair, range across the density sweep
7. order-of-magnitude span of the density sweep
8. best-cell speedup over $\cell{\rB}{\rB}$ at the sparsest density point
9. crossover density, order of magnitude, stated as an approximate value
10. dense-tail win ratio over $\cell{\rB}{\rB}$ at the densest measured point
11. universe size held fixed for the density sweep
12. host and cache level the density sweep was measured on
13. repeat count per density point
14. pairs sampled per density point
15. count of asymmetric pairing cells raced
16. count of corpus shapes tested
17. count of asymmetric-cell measurement points, expected twenty-five
18. count of microarchitectures in the working-set sweep
19. count of asymmetric measurement points won by a work-avoidance strategy
20. total asymmetric measurement points, expected twenty-five
21. SIMD speedup over scalar $\cell{\rB}{\rB}$ at L2 residency, per host
22. SIMD speedup over scalar $\cell{\rB}{\rB}$ at DRAM residency, per host
23. work-avoidance speedup at L2 residency, per host
24. work-avoidance speedup at DRAM residency, per host
25. host count in the working-set sweep
26. list of ISAs covered by the working-set sweep
27. reference host for the cache-boundary guides
28. count of asymmetric pairing cells
29. count of asymmetric measurement points won by a SIMD-throughput kernel
30. total asymmetric measurement points
31. corpus shape held fixed across the working-set sweep
32. selection-cost budget, as a fraction of runtime
33. count of hosts the selection-cost sweep was measured on
34. per-pair selection cost, percent of runtime, range across hosts and corpora
35. selection-cost budget, as a fraction of runtime _(repeat)_
36. per-tile selection cost, percent of runtime
37. probe-and-commit selection cost, percent of runtime
38. regret ranking and magnitude, probe-and-commit vs.\ per-tile vs.\ all-bitmap-always vs.\ a closed-form per-pair cost model
39. geomean speedup, filter applied unconditionally
40. worst-case speedup, filter applied unconditionally
41. count of corpora harmed when the filter is applied unconditionally
42. total corpora in the gating comparison
43. residual factor from the bucket oracle, expected order approximately 1.5$\times$
44. host count in the selection-cost sweep
45. selection-cost budget, as a fraction of runtime _(repeat)_
46. count of corpora in the gating comparison

## results_b (84)

1. ...
2. count of larger modern graph corpora added to the twelve \texttt{real-roaring-datasets} corpora
3. total corpus count, expected 17
4. density span, min--max across corpora
5. universe span, min--max across corpora, in bits
6. count of corpora beaten by the deployed selector, out of the total
7. deployed ratio range, min\texttimes--max\texttimes
8. deployed median ratio
9. oracle ratio range, min\texttimes--max\texttimes
10. oracle median ratio
11. names of corpora excluded from the headline range pending a measurement-drift fix, e.g. \texttt{uscensus2000} and \texttt{dimension\_033}
12. winning-cell histogram, e.g. $\cell{\rB}{\rB}$ \texttimes n corpora, $\cell{\rB}{\rS}$ \texttimes n, $\cell{\rB}{\rR}$ \texttimes n, $\cell{\rS}{\rS}$ \texttimes n, $\cell{\rR}{\rR}$ \texttimes n
13. fraction of corpora where $\cell{\rW}{\rW}$ underperforms 1.0\texttimes, e.g. n of total
14. $m$
15. $d$
16. cell
17. ratio
18. ratio _(repeat)_
19. $m$ _(repeat)_
20. $d$ _(repeat)_
21. cell _(repeat)_
22. ratio _(repeat)_
23. ratio _(repeat)_
24. $m$ _(repeat)_
25. $d$ _(repeat)_
26. cell _(repeat)_
27. ratio _(repeat)_
28. ratio _(repeat)_
29. $m$ _(repeat)_
30. $d$ _(repeat)_
31. cell _(repeat)_
32. ratio _(repeat)_
33. ratio _(repeat)_
34. $m$ _(repeat)_
35. $d$ _(repeat)_
36. cell _(repeat)_
37. ratio _(repeat)_
38. ratio _(repeat)_
39. $m$ _(repeat)_
40. $d$ _(repeat)_
41. cell, exp.\ $\cell{\rB}{\rB}$
42. ratio, exp.\ $<$1.0\texttimes
43. ratio _(repeat)_
44. dense corpus
45. $m$ _(repeat)_
46. $d$ _(repeat)_
47. cell _(repeat)_
48. ratio _(repeat)_
49. ratio _(repeat)_
50. remaining corpora
51. total corpus count, expected 17 _(repeat)_
52. count
53. number of round-robin-interleaved repeats
54. density range, e.g. 6--17\%, where CRoaring underperforms more than an identically dense popcount workload should explain
55. corpus name(s) where the census is sharpest, e.g. census1881
56. global density for that corpus
57. array container count
58. bitset container count, expected zero or near-zero
59. run container count
60. total chunk count
61. global density at which the mispricing occurs, expressed as a percentage
62. $d$ _(repeat)_
63. n
64. n, exp.\ 0
65. n _(repeat)_
66. n _(repeat)_
67. $d$ _(repeat)_
68. n _(repeat)_
69. n _(repeat)_
70. n _(repeat)_
71. n _(repeat)_
72. dense corpus (as Table~\ref{tab:corpora})
73. $d$ _(repeat)_
74. n _(repeat)_
75. n _(repeat)_
76. n _(repeat)_
77. n _(repeat)_
78. remaining corpora _(repeat)_
79. count of corpora beaten by the deployed selector
80. total corpus count
81. count of real corpora won by $\cell{\rB}{\rB}$, out of the total tested
82. variant-major ratio range across the genomic corpora, e.g. UShER and gnomAD chr21
83. haplotype-major 1KGP3 chr20 ratio, expected shape "below 1.0\texttimes"
84. closest measured regret to the bucket oracle, expected order of magnitude "1.5\texttimes"

## discussion (2)

1. selector-to-bucket-oracle regret bound stated as a multiple, e.g. "1.5$\times$"
2. number and identity of microarchitectures used for the real-corpus comparison

## methods (28)

1. stated count of symmetric non-baseline cells, expected "ten"
2. zone-map bin width in bits
3. zone-map storage cost as a fraction of the bitmap it summarizes
4. zone-map bin width in bits _(repeat)_
5. rank-index storage cost as a fraction of the bitmap it indexes
6. tile width in rows
7. count of probe pairs sampled per tile before committing
8. stated selection-cost budget as a percentage of total runtime, from the design target this paper adopts
9. warm-up iteration count or criterion
10. smallest working-set size in the sweep
11. largest working-set size in the sweep
12. repeat count for synthetic per-cell sweeps
13. repeat count for real-corpus comparisons
14. stated dispersion tolerance used to flag a corpus as unquotable
15. count of corpora, of the total corpus set, where the winning cell itself changed between repeats
16. test\_storm check count
17. test\_storm failure count, expected "0"
18. test\_cells check count
19. test\_cells failure count, expected "0"
20. cross-architecture differential check count per host family
21. count of unmangled STORM\_-prefixed exported symbols
22. count of defined mangled (C++) symbols, expected "0"
23. count of microarchitectures used across the whole paper, expected "four"
24. per-host microarchitecture name, core/cluster configuration, and nominal clock speed
25. per-host L1d, L2, and shared/last-level cache sizes
26. compiler name and version per host
27. compiler flags used, and whether native-architecture dispatch (\texttt{-march=native} or equivalent) was enabled or disabled for a given reported number
28. CRoaring version pin, expected "v4.7.2" pending re-verification against current \texttt{master}

