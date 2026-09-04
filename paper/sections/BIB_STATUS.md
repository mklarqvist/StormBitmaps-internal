# Bibliography verification status

`references.bib` was rebuilt from scratch for this manuscript (`storm.tex`). The old
`references.bib` belonged to an unrelated paper and is preserved as
`references_scaffold.bib.bak`. This file retains the initial audit as a historical snapshot and
records later literature passes as dated addenda. Current key resolution and BibTeX diagnostics are
checked by rebuilding `storm.tex`; counts in the initial snapshot below are not current totals.

**Key-mismatch check:** at the time this file was written, every `\cite{...}` key across
`sections/introduction.tex`, `sections/results_a.tex`, `sections/results_b.tex`,
`sections/methods.tex`, and `sections/discussion.tex` matched a `references.bib` key exactly — no
mismatches remain. (`results_b.tex` earlier used the short forms `chambi2016`/`lemire2018`
instead of `chambi2016roaring`/`lemire2018roaring`; that was corrected in the `.tex` file by a
concurrent editing pass before this bib was finalized. Re-run the grep below if `sections/*.tex`
changes again after this file is written.)

```
grep -oE '\\cite\{[^}]+\}' sections/*.tex | sed -E 's/.*\\cite\{([^}]+)\}/\1/' | tr ',' '\n' | sort -u
```

## Initial audit: cited by at least one section file (historical snapshot)

| Key | Verified | Source(s) used | Notes |
|---|---|---|---|
| `chambi2016roaring` | Yes | DBLP `journals/spe/ChambiLKG16`; Wiley DOI page | SPE **46(5)**:709–719, 2016, DOI 10.1002/spe.2325. Also on arXiv:1402.6407. **NARRATIVE.md §8 originally cited this as "46(11):1547–1569"** — that is the *other* Roaring paper's volume/pages (see `lemire2016roaring`); the conflation was flagged by the coordinator and is corrected here. |
| `lemire2016roaring` | Yes | arXiv:1603.06549 abstract (journal-ref); DBLP search API (`journals/spe/LemireKK16`) | SPE **46(11)**:1547–1569, 2016, DOI 10.1002/spe.2402. This is the **third, separate** Roaring paper (Lemire, Ssi-Yan-Kai, Kaser) — distinct from both `chambi2016roaring` and `lemire2018roaring`. Not yet cited by key in any `.tex` file (see "Verified but not yet cited" below — it is in the cited-keys table only because the coordinator's correction named it explicitly; grep confirms 0 `\cite{lemire2016roaring}` occurrences as of this pass). |
| `lemire2018roaring` | Yes | DBLP `journals/spe/LemireKKDOSK18` (full author list) | SPE **48(4)**:867–895, 2018, DOI 10.1002/spe.2560. Full 7-author list confirmed (Lemire, Kaser, Kurz, Deri, O'Hara, Saint-Jacques, Ssi-Yan-Kai) — original draft (`intro_refs.txt`) had this right. |
| `mula2018popcount` | Yes | Oxford Academic (Computer Journal) abstract page; arXiv:1611.07612 | Computer Journal 61(1):111–120, 2018, DOI 10.1093/comjnl/bxx046. |
| `goodwin2017bitfunnel` | Yes | ACM DL DOI page (via search snippet); Microsoft Research publication page | SIGIR 2017, pp. 605–614, DOI 10.1145/3077136.3080789. Full 7-author list confirmed. |
| `raducanu2013microadaptivity` | Yes | ACM DL DOI page (via search snippet); CMU course-hosted PDF | SIGMOD 2013, pp. 1231–1242, DOI 10.1145/2463676.2465292. |
| `jacobson1989succinct` | Yes | DBLP `conf/focs/Jacobson89`; ACM DL DOI page | FOCS 1989, pp. 549–554, DOI 10.1109/SFCS.1989.63533. Note: paper title is "Space-efficient **static trees and graphs**" — matches the `.tex` citation context (rank9 lineage), not "static trees" alone. |
| `clark1996compact` | Partially | Web search corroboration (Cambridge UP "Compact Data Structures" ref list; KIPDF-hosted thesis scan) | Title "Compact Pat Trees," PhD thesis, University of Waterloo, 1996 — confirmed by two independent secondary sources, but **no page count or thesis/library record was fetched directly** (University of Waterloo's institutional repository was not queried). Treat the thesis-level facts (author, title, institution, year) as verified; no further bibliographic fields exist to check for a thesis without pages/advisor listed in either citing source. |
| `vigna2008broadword` | Yes | Springer chapter page (via search snippet); ResearchGate/DocsLib mirrors | WEA 2008, LNCS vol. 5038, pp. 154–168, DOI 10.1007/978-3-540-68552-4_12. |
| `bayardo2007allpairs` | Yes | DBLP `conf/www/BayardoMS07` (direct fetch) | WWW 2007, pp. 131–140, DOI 10.1145/1242572.1242591. |
| `xiao2008ppjoin` | Yes | DBLP `conf/www/XiaoWLY08` (direct fetch) | WWW 2008, pp. 131–140, DOI 10.1145/1367497.1367516. (Page range genuinely matches Bayardo's — confirmed independently for each record, not a copy-paste artifact.) |
| `haque2011tanimoto` | Yes | Web search cross-check (ACS pubs page, PubMed) | Exact title is **"Anatomy of High-Performance 2D Similarity Calculations"** — the task brief's recalled title ("SIML: A Fast SIMD Algorithm...") was wrong; corrected here. JCIM 51(9):2345–2351, 2011, DOI 10.1021/ci200235e. |
| `dalke2019chemfp` | Yes | Springer (J. Cheminformatics) article page (via search snippet); ChemRxiv preprint page | J. Cheminformatics 11:76, 2019, DOI 10.1186/s13321-019-0398-8. |
| `swamidass2007bounds` | Yes | ACS Publications DOI page (via search snippet); PubMed | JCIM 47(2):302–317, 2007, DOI 10.1021/ci600358f. |
| `fu1995` | Yes | Semantic Scholar / PubMed cross-check | Theor. Popul. Biol. 48(2):172–197, 1995, DOI 10.1006/tpbi.1995.1025. Exact title "Statistical Properties of Segregating Sites" confirmed (not "...of the Site-Frequency Spectrum", a different, later paper that came up in search noise). |
| `tomahawk_github` | Yes (as software, not a paper) | github.com/mklarqvist/Tomahawk (repository itself; not independently re-fetched this pass, per dm_refs.txt's prior note that no paper/preprint exists) | `@misc`, software repository, created 2017-07-17. Correctly has no DOI/venue — matches NARRATIVE.md/dm_refs.txt guidance not to invent one. |

## Initial audit: verified but not yet cited by key (historical snapshot)

These are named explicitly in the task's "works to verify and include" list (drawn from
NARRATIVE.md §8's "Selection and adaptivity," "Rank/select," and "Compressed bitmap lineage" /
"Domain anchor" groups) but no current `\cite{...}` in the section drafts invokes them under these
keys. They are included in `references.bib` per that explicit instruction, verified, and flagged
here rather than silently added. If the final draft does not end up citing one of these, it should
be dropped from the bib before submission; if a section is still being drafted (e.g. Methods §4.5's
census1881 loader-correctness check, which per `SECTION_SPEC.md` line ~684 should probably cite
`lemire2016roaring`/arXiv:1603.06549 rather than the `chambi2016roaring` currently in
`methods.tex:150`), wire the key in rather than leaving it orphaned.

| Key | Verified | Source(s) used | Notes |
|---|---|---|---|
| `lemire2016roaring` | Yes | See above | Also listed above since the coordinator named it explicitly; repeated here for visibility as an orphaned key — 0 `\cite{}` occurrences found. |
| `aberger2016emptyheaded` | Yes | DBLP-sourced search snippet (ACM DL proceedings page) | SIGMOD 2016, pp. 431–446, DOI 10.1145/2882903.2915213. 4-author version (Aberger, Tu, Olukotun, Ré). |
| `aberger2017emptyheaded` | Yes | ACM DL DOI page (via search snippet) | TODS 42(4), article 20, DOI 10.1145/3129246. **6-author version** (adds Lamb, Nötzli) — the SIGMOD and TODS papers have different author lists; both entries reflect their own byline, not a copy of each other. |
| `niu2019iaspgemm` | Yes | Author's own publication page (zhen-xie.com); ACM DL (via search snippet) | ICS 2019, pp. 94–105, DOI 10.1145/3330345.3330354. Authors are **Xie, Tan, Liu, Sun** — "Niu" (the key's namesake, from NARRATIVE.md's shorthand) does not appear on the actual author list; NARRATIVE.md's short citation form was imprecise. Key retained as given by the task brief, but the `author` field reflects the real byline. |
| `vuduc2005oski` | Yes | IOPscience article page (via search snippet) | J. Phys.: Conf. Ser. 16:521–530, 2005, DOI 10.1088/1742-6596/16/1/071. |
| `zhou2013poppy` | Partially | Springer chapter page and CMU author page (via search snippets); direct Springer fetch blocked by publisher login wall | SEA 2013, LNCS vol. 7933, DOI 10.1007/978-3-642-38527-8_15 all confirmed. **Page range within the proceedings could not be confirmed** — Springer's page redirected to an authentication wall and no secondary source stated the exact pp. range, so `pages` is omitted from the bib entry rather than guessed. |
| `wu2006wah` | Yes | ACM DL DOI page (via search snippet) | TODS 31(1):1–38, 2006, DOI 10.1145/1132863.1132864. Title is "Optimizing Bitmap Indices with Efficient Compression" (the canonical WAH paper). |
| `colantonio2010concise` | Yes | DBLP search API (direct JSON fetch) | **Venue correction:** the task brief said "Information Systems 2010"; DBLP's authoritative record gives **Information Processing Letters 110(16):644–650, 2010**, DOI 10.1016/j.ipl.2010.05.018. Used the verified venue, not the brief's recalled one. |
| `lemire2010ewah` | Yes | Lemire's own publication page; arXiv:0901.3751 | Data & Knowledge Engineering 69(1):3–28, 2010, DOI 10.1016/j.datak.2009.08.006. |
| `purcell2007plink` | Yes | Cell/AJHG PDF (via search snippet); PubMed | AJHG 81(3):559–575, 2007, DOI 10.1086/519795. Full 11-author list included. |
| `chang2015plink2` | Yes | Oxford Academic (GigaScience) PDF page (via search snippet); GigaScience article page | GigaScience 4:7, 2015, DOI 10.1186/s13742-015-0047-8. |

## Initial-pass summary (historical)

- **26 entries** in `references.bib`.
- **25 of 26 fully verified** (author list, title, venue, volume/issue, pages, year, DOI all
  confirmed against a publisher/DBLP/arXiv source).
- **1 partially verified**: `zhou2013poppy` — venue, volume (LNCS 7933), year, and DOI are
  confirmed; the in-proceedings page range could not be sourced and was left blank rather than
  guessed.
- **1 partially verified on a different axis**: `clark1996compact` — a PhD thesis with no
  DOI/pages to check; author/title/institution/year confirmed via two independent secondary
  citations rather than a primary UWaterloo repository record.
- **Key mismatches found and resolved during this pass:** `sections/results_b.tex` originally used
  `chambi2016`/`lemire2018` (missing the `roaring` suffix used everywhere else); a concurrent
  editing pass corrected this in the `.tex` file before this bib was finalized, so no mismatch
  remains as of this writing.
- **Three-Roaring-papers disambiguation:** `chambi2016roaring` (SPE 46(5), 2016),
  `lemire2016roaring` (SPE 46(11), 2016 — the "Consistently faster and smaller" paper, not
  currently cited by key), and `lemire2018roaring` (SPE 48(4), 2018) are all distinct entries with
  independently verified metadata; none share author lists or volume/issue/page data.
- **Corrections made against the recalled (unverified) drafts in `intro_refs.txt` /
  `results_a_refs.txt` / `dm_refs.txt`:**
  - `chambi2016roaring`'s volume/issue/pages (46(5):709–719, not 46(11):1547–1569, which belongs
    to `lemire2016roaring`).
  - `haque2011tanimoto`'s exact title ("Anatomy of High-Performance 2D Similarity Calculations,"
    not the recalled "SIML..." title).
  - `colantonio2010concise`'s venue (Information Processing Letters, not Information Systems).
  - `niu2019iaspgemm`'s actual author list (Xie, Tan, Liu, Sun — not "Niu" as the NARRATIVE.md
    shorthand implied).

## 2026-09-04 competitor and prior-art audit

A three-way literature audit covered row-oriented set intersection, bitmap and hybrid
representations, and column-oriented all-pairs candidate generation. The following additions were
checked against author-hosted papers, institutional publication records, DBLP, PVLDB, or publisher
DOI metadata before being added and cited in the manuscript.

| Key | Verified source | Role in the manuscript |
|---|---|---|
| `karp1995bitvector` | Microsoft Research record; DOI 10.1109/SFCS.1995.492663 | Historical thresholded all-pairs sparse bit-vector problem. |
| `demaine2000adaptive` | Authors' Waterloo publication record and paper | Instance-adaptive sorted-set operations and certificate-based analysis. |
| `ding2011intersection` | PVLDB 4(4):255–266; DOI 10.14778/1938545.1938550 | Preprocessed word-parallel, output-sensitive in-memory intersection. |
| `culpepper2010intersection` | Author-hosted TOIS paper; DOI 10.1145/1877766.1877767 | Hybrid compressed-list and bitmap intersection for inverted indexes. |
| `inoue2014simdintersection` | PVLDB 8(3):293–304; DOI 10.14778/2735508.2735518 | SIMD sorted-array intersection via branch reduction. |
| `lemire2016simdintersection` | Wiley metadata and arXiv:1401.6399; DOI 10.1002/spe.2326 | SIMD and galloping intersection of sorted postings. |
| `gustavson1978spgemm` | IBM Research and ACM metadata; DOI 10.1145/355791.355796 | Canonical sparse multiplication and transposition lineage. |
| `buluc2011combinatorialblas` | GraphBLAS/CombBLAS record; DOI 10.1177/1094342011403516 | Sparse matrix and graph-algebra execution family. |
| `an2019skipintersection` | Author paper, DBLP and IEEE DOI 10.1109/HPEC.2019.8916307 | Exact column/wedge all-pairs common-neighbour counting. |
| `sarawagi2004setjoins` | Author-hosted SIGMOD paper; DOI 10.1145/1007568.1007652 | Early exact inverted-index joins for intersection, Jaccard and cosine predicates. |
| `xiao2011ppjoin` | HKUST publication record; DOI 10.1145/2000824.2000825 | Full PPJoin positional and suffix-filtering treatment. |
| `wang2012adaptjoin` | SIGMOD metadata; DOI 10.1145/2213836.2213847 | Adaptive per-object prefix selection for threshold joins. |

The audit changed claim placement rather than merely enlarging the bibliography: the Introduction
now identifies the closest prior execution families, Methods states the exact specialization of
each mechanism, Results scopes the SIMD and posting comparisons, and Discussion states the paper's
two-resolution systems contribution compositionally.
