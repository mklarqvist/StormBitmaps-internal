| corpus | disjoint | bitmap/row | ilp8 ns | bloom | blocked bloom | **coarse zone map** | existing zone map |
|---|---:|---:|---:|---:|---:|---:|---:|
| com-LiveJournal | 99.9% | 493 kB | 25.5 | 2.73x | 2.26x | **3.70x** | 1.81x |
| soc-Pokec | 99.9% | 199 kB | 36.3 | 2.82x | 2.21x | **3.46x** | 1.64x |
| com-Orkut | 99.5% | 375 kB | 92.7 | 2.64x | 1.86x | **3.05x** | 1.41x |
| as-skitter | 99.4% | 207 kB | 10.4 | 1.60x | 1.35x | **2.08x** | 0.93x |
| uscensus2000 | 100.0% | 4,514 kB | 6.2 | 1.62x | 1.44x | **2.08x** | 1.06x |
| wiki-Talk | 95.1% | 292 kB | 25.1 | 1.53x | 1.36x | **1.38x** | 0.87x |
| dimension_003 | 100.0% | 472 kB | 8.1 | 0.71x | 0.52x | **1.20x** | 1.11x |
| census1881 | 96.9% | 522 kB | 97.8 | 0.31x | 0.23x | **0.63x** | 0.28x |
| census-income | 24.8% | 24 kB | 1894.5 | 0.22x | 0.16x | **0.31x** | 0.32x |
| weather_sept_85 | 34.7% | 124 kB | 2866.8 | 0.25x | 0.18x | **0.29x** | 0.25x |

coarse zone map wins 7/10; bloom beats coarse on 1/10; blocked beats plain bloom on 0/10
