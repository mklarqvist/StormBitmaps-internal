# Benchmark corpora

**No dataset is committed to this repository.** Fetch them with
[`bench/fetch_corpora.sh`](../bench/fetch_corpora.sh), then run
[`bench/run_corpora.sh`](../bench/run_corpora.sh).

This file describes each corpus in enough detail to re-identify or reconstruct
it if a URL rots — provenance, citation, measured shape, and the exact
conversion command. Measured columns come from `tools/sets2bin.py`; where they
can be cross-checked against a published figure, they match (census1881:
universe 4,277,806 and mean |Xi| 5,019.3, matching arXiv:1603.06549).

Results: [`results/CORPORA.md`](../results/CORPORA.md).

---

## Group 1 — `real-roaring-datasets` (the CRoaring benchmark corpora)

**Why these specifically:** these are the files CRoaring's own microbenchmark
harness runs by default — its README names `census1881` as the default pick. A
head-to-head on the incumbent's chosen data cannot be dismissed as favourable
dataset selection.

- **Repository:** <https://github.com/RoaringBitmap/real-roaring-datasets>
- **Layout:** one `.zip` per corpus; inside, one text file per bitmap containing
  comma-separated 32-bit ids. Read by `tools/sets2bin.py --format csvdir`.
- **Papers that use them:** Chambi, Lemire, Kaser, Godin, *Better bitmap
  performance with Roaring bitmaps*, SPE 2016 (arXiv:1402.6407); Lemire,
  Ssi-Yan-Kai, Kaser, *Consistently faster and smaller compressed bitmaps with
  Roaring*, SPE 2016 (arXiv:1603.06549).
- **`_srt` variants** are the row-sorted versions the Roaring papers report
  separately. Sorting raises clustering, so each pair gives two points on the
  clustering axis.

| corpus | sets | universe m | mean \|Xi\| | density | origin |
|---|---:|---:|---:|---:|---|
| `census1881` | 200 | 4,277,806 | 5,019 | 1.2e-03 | 1881 Canadian census, categorical attributes |
| `census1881_srt` | 200 | 4,277,735 | 3,404 | 8.0e-04 | as above, rows sorted |
| `census-income` | 200 | 199,523 | 34,610 | 1.7e-01 | US census income (UCI "Adult"-family), attributes |
| `census-income_srt` | 200 | 199,523 | 30,464 | 1.5e-01 | as above, rows sorted |
| `weather_sept_85` | 200 | 1,015,367 | 64,353 | 6.3e-02 | September 1985 weather observations, binned attributes |
| `weather_sept_85_srt` | 200 | 1,015,367 | 80,540 | 7.9e-02 | as above, rows sorted |
| `wikileaks-noquotes` | 200 | 1,353,179 | 1,377 | 1.0e-03 | WikiLeaks cable corpus, term/attribute postings |
| `wikileaks-noquotes_srt` | 200 | 1,353,133 | 1,440 | 1.1e-03 | as above, rows sorted |
| `uscensus2000` | 200 | 36,974,578 | 30 | **8.1e-07** | US Census 2000; **sparsest and largest-universe corpus in the set** |
| `dimension_003` | 15,482 | 3,866,847 | 91 | 2.4e-05 | Druid-derived dimension column |
| `dimension_008` | 5,240 | 3,866,845 | 347 | 9.0e-05 | Druid-derived dimension column |
| `dimension_033` | 173 | 3,866,847 | 22,352 | 5.8e-03 | Druid-derived dimension column |

`bitsets_1925630_96.gz` also ships in that repository but is not used here.

Conversion (all of them):

```sh
.venv/bin/python tools/sets2bin.py <unzipped-dir> -o <name>.bin --format csvdir
```

---

## Group 2 — SNAP graphs (neighbourhood intersection)

Each vertex's adjacency list is a set; universe = |V|. All-pairs neighbourhood
intersection is common-neighbour counting (link prediction, similarity). The
power-law degree distribution supplies the skewed cardinality spectrum without
having to construct it.

- **Collection:** Stanford Large Network Dataset Collection,
  <https://snap.stanford.edu/data/>
- **Citation:** Leskovec & Krevl, *SNAP Datasets: Stanford Large Network Dataset
  Collection*, <http://snap.stanford.edu/data>, 2014.
- **Format:** gzipped two-column tab-separated edge list, `#` comment header.
  Read by `tools/sets2bin.py --format edgelist`.
- **Terms:** free for research use, citation requested.

| corpus | file | \|V\| | \|E\| | measured universe | measured density | notes |
|---|---|---:|---:|---:|---:|---|
| `as-skitter` | `as-skitter.txt.gz` (32 MB) | 1,696,415 | 11,095,298 | 1,696,415 | 9.1e-06 | CAIDA skitter internet AS topology, 2005. Undirected → `--symmetrize` |
| `wiki-Talk` | `wiki-Talk.txt.gz` (16 MB) | 2,394,385 | 5,021,410 | 2,394,385 | 2.2e-05 | Wikipedia talk-page edits. Directed; only 67,492 vertices have out-degree ≥ 2 |
| `soc-Pokec` | `soc-pokec-relationships.txt.gz` (126 MB) | 1,632,803 | 30,622,564 | 1,632,804 | 1.6e-05 | Pokec social network (Slovakia). Directed. **Lowest skew of the graphs** — top 1% of sets hold only 8.8% of elements |
| `com-LiveJournal` | `bigdata/communities/com-lj.ungraph.txt.gz` (119 MB) | 3,997,962 | 34,681,189 | 4,036,538 | 5.1e-06 | LiveJournal friendship, ground-truth communities. Undirected → `--symmetrize` |
| `com-Orkut` | `bigdata/communities/com-orkut.ungraph.txt.gz` (427 MB) | 3,072,441 | 117,185,083 | 3,072,627 | 2.7e-05 | Orkut social network. Undirected → `--symmetrize`. **Largest input here**; conversion needs ~10 GB RAM and several minutes |

Note the community graphs live under `bigdata/communities/`, not the top-level
`data/` path — a URL shape that has changed before.

Conversion:

```sh
# undirected (com-*, as-skitter)
.venv/bin/python tools/sets2bin.py as-skitter.txt -o as-skitter.bin \
    --format edgelist --symmetrize --min-card 2
# directed (wiki-Talk, soc-Pokec)
.venv/bin/python tools/sets2bin.py wiki-Talk.txt -o wiki-Talk.bin \
    --format edgelist --min-card 2
```

`--min-card 2` drops degree-0/1 vertices, which contribute no meaningful
intersection and would otherwise dominate the row count.

---

## Group 3 — genomic (present for scoping, not for the headline)

Not fetched by the script; built locally from a 1000 Genomes VCF via
`tools/vcf2bin.sh` + `tools/gt2bin.py`.

| corpus | layout | universe | density | why it is here |
|---|---|---:|---:|---|
| `chr20.bin` | variant-major | 5,008 haplotypes | 3.5e-02 | 1/i site frequency spectrum, 43.6% singletons — but universe is 626 bytes, L1-resident |
| `chr20_hap.bin` | haplotype-major | ~1,048,576 sites | 3.1e-02 | large universe, but density 3.1% — **nothing beats all-bitmap (0.93×)** |

These two are the evidence for the scoping statement: **human variant data gives
a large universe *or* sparsity, never both.** That is a property of human
variation, not of the method — Group 1 and 2 reach the target regime routinely.

Source: 1000 Genomes Phase 3,
<https://ftp.1000genomes.ebi.ac.uk/vol1/ftp/release/20130502/>. Citation: 1000
Genomes Project Consortium, *A global reference for human genetic variation*,
Nature 526:68–74, 2015.

---

## Group 4 — KONECT graphs and UShER (added, measured)

Fetched and converted after the original survey; all verified structurally
(row count matches header, zero non-ascending rows, zero out-of-range values).
Figures are derived from the STORMBIN files themselves, not from source headers.

| corpus | source | universe m | rows | mean \|Xi\| | density | regime |
|---|---|---:|---:|---:|---:|---|
| `dbpedia-link` | [KONECT](http://konect.cc/networks/dbpedia-link/) | 18,268,993 | 4,384,102 | 29.8 | 1.63e-06 | **QUALIFIES** |
| `wikipedia_link_en` | [KONECT](http://konect.cc/networks/wikipedia_link_en/) | 11,206,012 | 5,978,043 | 46.4 | 4.15e-06 | **QUALIFIES** |
| `livejournal-groupmemberships` | [KONECT](http://konect.cc/networks/livejournal-groupmemberships/) | 7,489,074 | 2,197,915 | 48.7 | 6.50e-06 | **QUALIFIES** |
| `usher_sarscov2` | [UCSC UShER](https://hgdownload.soe.ucsc.edu/goldenPath/wuhCor1/UShER_SARS-CoV-2/) | 8,451,771 | 29,411 | 15,769.3 | 1.87e-03 | out of regime |
| `msprime_{10k,100k,1M}` | `tools/msprime2bin.py` | 2e4 / 2e5 / 2e6 | 42k / 51k / 15.6k | 1,887 / 15,720 / 136,288 | 0.094 / 0.079 / 0.068 | out of regime |

`dbpedia-link` and `wikipedia_link_en` are `% asym` at source and are converted
**without** `--symmetrize`, matching the convention for directed graphs in Group 2.

`livejournal-groupmemberships` is bipartite (`group user`, columns swapped before
conversion so rows are groups). Its universe is **inflated**: `sets2bin.py` uses a
single id space for both columns, so 7,489,074 is the max group id rather than the
3,201,203 users that are the true member domain. Density is understated ~2.3x.
Forcing `--universe 3201204` crashes in `_rows_from_edges` — a real limitation of
the tool for bipartite input.

`usher_sarscov2` misses the regime on density but is the sharpest real example of
the C25 mechanism: mean cardinality 15,769 against a **median of 404**, because a
few near-universal variants (max 8,428,858 carriers = 99.7% of genomes) dominate
the mean. The typical variant sits at density 4.8e-5, inside the regime.

Conversion: `tools/vcf2bin_usher.py` (UShER), `tools/msprime2bin.py` (coalescent),
`tools/sets2bin.py --format edgelist --min-card 2` (KONECT).

## Candidates identified but not yet used

| dataset | universe | why it is interesting | obstacle |
|---|---:|---|---|
| UShER SARS-CoV-2 MAT | 8,451,771 genomes | the one *genomic* dataset reaching the target regime; fully open, no login | 678 MB variant-major VCF; density unmeasured. <https://hgdownload.soe.ucsc.edu/goldenPath/wuhCor1/UShER_SARS-CoV-2/> |
| com-Friendster (SNAP) | 65,608,366 | largest clean social graph, d ≈ 8.4e-07 | 9.4 GB gzip |
| twitter-2010 (LAW) | 41,652,230 | documented extreme degree spread (max out-degree 2,997,469) | BVGraph format, needs the Java WebGraph library to export an edge list |
| ClueWeb09-gap posting lists | ~1e6 | the corpus Lemire's `SIMDCompressionAndIntersection` benchmarks use | host `boytsov.info` was unreachable when checked |
| Wikipedia `categorylinks` | 7.2M articles | open, no registration, genuine posting-list structure | 2.5 GB SQL dump, needs parsing |
| msprime / stdpopsim | arbitrary | principled coalescent generator; would replace the hand-rolled 1/i draw in `kernels/storm_gen.cpp` | not a download — a simulation step to build |

UK Biobank, All of Us and FinnGen all clear 10⁶ haplotypes but are
cloud-egress-restricted; gnomAD and TOPMed BRAVO are aggregate-only by policy.
None can become a portable benchmark artifact.
