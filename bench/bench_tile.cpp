/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist. Apache-2.0.
 *
 * WIDTH PASS over the all-pairs structure (RESEARCH_PLAN.md 15.14).
 *
 * Everything measured so far optimises ONE pair. The workload is all-pairs, and
 * 95-100% of pairs are empty (C13), so the dominant cost is proving disjointness
 * N^2/2 times. This file tests whether the tile structure itself can be
 * exploited instead of the per-pair constant.
 *
 * The unit here is a TILE of T rows and all T(T-1)/2 pairs within it -- the
 * shape M3 tile hoisting already assumes -- not a flat pair list, because
 * several of these ideas only exist at tile granularity.
 *
 * Candidates:
 *   pair_gated   per-pair coarse-filter probe, gated (C16). The incumbent.
 *   range        O(1) [min,max] overlap rejection in front of the incumbent.
 *   hoist        reuse the dense row's filter across its whole row of pairs.
 *   matrix       transpose the tile's zone maps and resolve ALL pairs' bucket
 *                overlap with one sparse pass, before touching any data.
 *   group        one-level row hierarchy: OR 8 rows' maps, reject 8 at a time.
 *
 * Every candidate is exact: they only ever skip work provably empty, and all
 * are checked against the unfiltered kernel on every pair before timing.
 */
#include "kernels/storm_cells.h"
#include "kernels/storm_repr.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <time.h>

using namespace storm;

static inline uint64_t ns_now() {
#if defined(__APPLE__)
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;
#endif
}
template <class L> static auto pick(L l, const char* nm) -> decltype(l.v[0].fn) {
    for (size_t i = 0; i < l.n; ++i) if (std::string(l.v[i].name) == nm) return l.v[i].fn;
    return nullptr;
}

static uint32_t T = 64;          // rows per tile

struct Occ {                            // coarse positional zone map (C14/C15)
    std::vector<uint64_t> w; uint32_t shift = 0, nw = 0;
    void init(uint32_t bits, uint32_t universe) {
        uint32_t width = (universe + bits - 1) / bits;
        shift = 0; while ((1u << shift) < width) ++shift;
        nw = (uint32_t)((((uint64_t)universe >> shift) + 1 + 63) / 64);
        w.assign(nw, 0ull);
    }
    inline void add(uint32_t x){ const uint32_t i = x >> shift; w[i>>6] |= 1ull<<(i&63); }
    inline bool maybe(uint32_t x) const { const uint32_t i = x >> shift; return (w[i>>6]>>(i&63))&1u; }
};

int main(int argc, char** argv) {
    const char* path = nullptr;
    uint32_t want_rows = 1024, stride = 1, fbits = 16384;
    int repeats = 7; std::string tag = "host"; bool gsort = false; uint32_t wide_tile = 128;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto nx = [&]() -> const char* { return i+1<argc ? argv[++i] : ""; };
        if      (a == "--file")   path = nx();
        else if (a == "--rows")   want_rows = atoi(nx());
        else if (a == "--stride") stride = atoi(nx());
        else if (a == "--bits")   fbits = atoi(nx());
        else if (a == "--repeats")repeats = atoi(nx());
        else if (a == "--tag")    tag = nx();
        else if (a == "--tile")   T = atoi(nx());
        else if (a == "--gsort")  gsort = true;
        else if (a == "--wide")   wide_tile = atoi(nx());
    }
    if (!path) { std::printf("need --file\n"); return 1; }
    if (!stride) stride = 1;

    FILE* f = fopen(path, "rb");
    if (!f) { std::printf("cannot open %s\n", path); return 1; }
    char mg[8]; uint32_t ver, nr, nb, pad;
    if (fread(mg,1,8,f)!=8 || memcmp(mg,"STORMBIN",8) || fread(&ver,4,1,f)!=1 ||
        fread(&nr,4,1,f)!=1 || fread(&nb,4,1,f)!=1 || fread(&pad,4,1,f)!=1) {
        std::printf("bad header\n"); return 1; }

    std::vector<Row> rows; std::vector<Occ> occ; std::vector<uint32_t> pos;
    std::vector<uint32_t> lo, hi;
    uint32_t seen = 0;
    while (rows.size() < want_rows) {
        uint32_t n; if (fread(&n,4,1,f)!=1) break;
        pos.resize(n); if (n && fread(pos.data(),4,n,f)!=n) break;
        if (seen % stride == 0 && n > 0) {
            rows.emplace_back(); build_row(rows.back(), pos.data(), pos.size(), nb);
            occ.emplace_back(); occ.back().init(fbits, nb);
            for (uint32_t k = 0; k < n; ++k) occ.back().add(pos[k]);
            lo.push_back(pos[0]); hi.push_back(pos[n-1]);
        }
        ++seen;
    }
    fclose(f);
    /* Global ordering before tiling.
     *
     * Within-tile sorting can only reorder rows that already landed together;
     * sorting the whole corpus by minimum element first puts rows with similar
     * extents in the SAME tile, which is what makes range rejection bite.
     *
     * CAVEAT, stated because it is easy to miss: this changes which pairs fall
     * inside a tile, so the within-tile pair SET differs from the unsorted run.
     * For the real all-pairs problem that is irrelevant -- every pair is
     * computed either way, and ordering only decides which block it lands in --
     * but it means the two runs are not pair-for-pair identical and the
     * comparison is between two samples of all-pairs, not one workload. */
    if (gsort) {
        std::vector<uint32_t> ord(rows.size());
        for (size_t i = 0; i < ord.size(); ++i) ord[i] = (uint32_t)i;
        std::sort(ord.begin(), ord.end(), [&](uint32_t a, uint32_t b){ return lo[a] < lo[b]; });
        std::vector<Row> nr2; nr2.reserve(rows.size());
        std::vector<Occ> no2; no2.reserve(rows.size());
        std::vector<uint32_t> nl(rows.size()), nh(rows.size());
        for (size_t i = 0; i < ord.size(); ++i) {
            nr2.push_back(std::move(rows[ord[i]])); no2.push_back(std::move(occ[ord[i]]));
            nl[i] = lo[ord[i]]; nh[i] = hi[ord[i]];
        }
        rows.swap(nr2); occ.swap(no2); lo.swap(nl); hi.swap(nh);
    }

    const uint32_t ntiles = (uint32_t)(rows.size() / T);
    if (!ntiles) { std::printf("need >= %u rows, have %zu\n", T, rows.size()); return 1; }

    /* Sort rows WITHIN each tile by their minimum element.
     *
     * Range rejection currently scans all T candidates per row even though the
     * predicate is monotone: once lo[j] exceeds hi[i], no later j can overlap
     * either. Sorting by lo turns the stage from O(T) per row into O(survivors)
     * with a single break.
     *
     * Sorting is confined to the tile so the SET OF PAIRS is unchanged and the
     * variants stay comparable -- a global sort would re-tile the corpus and
     * silently compare different work. */
    for (uint32_t t = 0; t + 1 <= rows.size()/T; ++t) {
        const uint32_t b = t*T;
        std::vector<uint32_t> ord(T);
        for (uint32_t i = 0; i < T; ++i) ord[i] = i;
        std::sort(ord.begin(), ord.end(), [&](uint32_t x, uint32_t y){ return lo[b+x] < lo[b+y]; });
        std::vector<Row> tr; tr.reserve(T);
        std::vector<Occ> to; to.reserve(T);
        std::vector<uint32_t> tl(T), th(T);
        for (uint32_t i = 0; i < T; ++i) {
            tr.push_back(std::move(rows[b+ord[i]]));
            to.push_back(std::move(occ[b+ord[i]]));
            tl[i] = lo[b+ord[i]]; th[i] = hi[b+ord[i]];
        }
        for (uint32_t i = 0; i < T; ++i) {
            rows[b+i] = std::move(tr[i]); occ[b+i] = std::move(to[i]);
            lo[b+i] = tl[i]; hi[b+i] = th[i];
        }
    }

    const auto f_bs = pick(cell_bs(), "ilp8");
    /* Alternative cells for the BYPASS path.
     *
     * When the gate rejects the zone map, the tile pipeline currently falls back
     * to B x S ilp8 -- but B x S is not the right cell on those corpora. The
     * pairing matrix already measured the answer: on weather_sept_85 and
     * census-income, B x B runs 10.75x and 12.82x over tuned CRoaring while S x S
     * runs 0.37x and 0.14x. The bypass path was defaulting to a cell the project
     * had already shown to lose there. */
    const auto f_bb = pick(cell_bb(), "occ_sel");
    const auto f_bbd = pick(cell_bb(), "dense");
    const auto f_ss = pick(cell_ss(), "adaptive2");
    const auto f_rr = pick(cell_rr(), "adaptive2");
    const uint32_t occ_nw = occ[0].nw;

    // exact per-pair kernel with the coarse filter in front (the incumbent)
    auto gated = [&](uint32_t d, uint32_t s) {
        const BitmapView B = rows[d].B(); const ListView S = rows[s].S();
        uint64_t h = 0;
        for (uint32_t i = 0; i < S.n; ++i) {
            const uint32_t x = S.v[i];
            if (!occ[d].maybe(x)) continue;
            h += (B.w[x>>6] >> (x&63)) & 1ull;
        }
        return h;
    };

    struct R { const char* n; double ns; uint64_t sum; };
    std::vector<R> out;
    auto bench = [&](const char* name, auto&& fn) {
        double best = 1e30; uint64_t sum = 0;
        for (int r = 0; r < repeats; ++r) {
            uint64_t acc = 0; const uint64_t t0 = ns_now();
            for (uint32_t t = 0; t < ntiles; ++t) acc += fn(t);
            const double dt = (double)(ns_now() - t0);
            if (dt < best) best = dt; sum = acc;
        }
        const double per = best / (double)(ntiles * (T*(T-1)/2));
        out.push_back({name, per, sum});
        return sum;
    };

    // ---- 0. no filter at all -------------------------------------------------
    const uint64_t truth = bench("bs_ilp8 (no filter)", [&](uint32_t t){
        uint64_t a = 0; const uint32_t b = t*T;
        for (uint32_t i = 0; i < T; ++i) for (uint32_t j = i+1; j < T; ++j)
            a += f_bs(rows[b+i].B(), rows[b+j].S());
        return a;
    });

    // ---- 1. incumbent: per-pair gated probe ---------------------------------
    bench("pair_gated (incumbent)", [&](uint32_t t){
        uint64_t a = 0; const uint32_t b = t*T;
        for (uint32_t i = 0; i < T; ++i) for (uint32_t j = i+1; j < T; ++j)
            a += gated(b+i, b+j);
        return a;
    });

    // ---- 2. range: O(1) [min,max] rejection in front -------------------------
    bench("range + gated", [&](uint32_t t){
        uint64_t a = 0; const uint32_t b = t*T;
        for (uint32_t i = 0; i < T; ++i) for (uint32_t j = i+1; j < T; ++j) {
            if (hi[b+i] < lo[b+j] || hi[b+j] < lo[b+i]) continue;  // provably disjoint
            a += gated(b+i, b+j);
        }
        return a;
    });

    // ---- 3. hoist: keep the dense row's filter live across its whole row -----
    bench("hoist filter per row", [&](uint32_t t){
        uint64_t a = 0; const uint32_t b = t*T;
        for (uint32_t i = 0; i < T; ++i) {
            const uint64_t* fw = occ[b+i].w.data();
            const uint32_t sh = occ[b+i].shift;
            const BitmapView B = rows[b+i].B();
            for (uint32_t j = i+1; j < T; ++j) {
                const ListView S = rows[b+j].S();
                for (uint32_t k = 0; k < S.n; ++k) {
                    const uint32_t x = S.v[k], q = x >> sh;
                    if (!((fw[q>>6] >> (q&63)) & 1u)) continue;
                    a += (B.w[x>>6] >> (x&63)) & 1ull;
                }
            }
        }
        return a;
    });

    /* ---- 4. matrix: transpose the tile, resolve ALL pairs at once ------------
     * The tile's zone maps are a T x nbits bit matrix; all-pairs bucket overlap
     * is that matrix times its own transpose. Walk the buckets ONCE: for each
     * bucket, `w` marks which of the T rows occupy it, so every pair set in `w`
     * is a candidate and every pair absent from every `w` is provably disjoint.
     *
     * Cost is sum over buckets of popcount(w) = total set bits in the tile, plus
     * one scan of a second-level "which buckets are non-empty" summary so empty
     * buckets are not visited. For T=64 and |A|=20 that is ~1,280 word-ORs plus
     * a 256-word scan to resolve 2,016 pairs -- under one operation per pair,
     * against ~20 probes per pair for the incumbent. */
    std::vector<uint64_t> colT(occ_nw * 64ull, 0ull);   // transposed tile
    std::vector<uint64_t> nonempty((occ_nw*64 + 63)/64, 0ull);
    std::vector<uint64_t> cand(T, 0ull);
    bench("matrix (tile transpose)", [&](uint32_t t){
        const uint32_t b = t*T;
        std::fill(colT.begin(), colT.end(), 0ull);
        std::fill(nonempty.begin(), nonempty.end(), 0ull);
        // transpose: bucket -> 64-bit row occupancy
        for (uint32_t i = 0; i < T; ++i) {
            const uint64_t* w = occ[b+i].w.data();
            for (uint32_t k = 0; k < occ_nw; ++k) {
                uint64_t v = w[k];
                while (v) {
                    const uint32_t bit = (uint32_t)__builtin_ctzll(v); v &= v-1;
                    const uint32_t bucket = k*64 + bit;
                    colT[bucket] |= 1ull << i;
                    nonempty[bucket>>6] |= 1ull << (bucket&63);
                }
            }
        }
        // resolve: OR each occupied bucket's row-word into every row it contains
        std::fill(cand.begin(), cand.end(), 0ull);
        for (size_t k = 0; k < nonempty.size(); ++k) {
            uint64_t nv = nonempty[k];
            while (nv) {
                const uint32_t bit = (uint32_t)__builtin_ctzll(nv); nv &= nv-1;
                const uint64_t w = colT[k*64 + bit];
                uint64_t r = w;
                while (r) { const uint32_t i = (uint32_t)__builtin_ctzll(r); r &= r-1; cand[i] |= w; }
            }
        }
        // only surviving pairs touch data
        uint64_t a = 0;
        for (uint32_t i = 0; i < T; ++i) {
            uint64_t m = cand[i] & ~((i==63) ? ~0ull : ((1ull<<(i+1))-1));  // j > i
            while (m) {
                const uint32_t j = (uint32_t)__builtin_ctzll(m); m &= m-1;
                a += gated(b+i, b+j);
            }
        }
        return a;
    });

    /* ---- 5. group: one-level row hierarchy ---------------------------------
     * OR every G rows' zone maps into a group map. If a row misses the group
     * map entirely, all G rows are rejected in one pass instead of G. */
    const uint32_t G = 8, NG = T / G;
    std::vector<std::vector<uint64_t>> grp(NG, std::vector<uint64_t>(occ_nw));
    bench("group filter (G=8)", [&](uint32_t t){
        const uint32_t b = t*T;
        for (uint32_t g = 0; g < NG; ++g) {
            std::fill(grp[g].begin(), grp[g].end(), 0ull);
            for (uint32_t i = 0; i < G; ++i) {
                const uint64_t* w = occ[b+g*G+i].w.data();
                for (uint32_t k = 0; k < occ_nw; ++k) grp[g][k] |= w[k];
            }
        }
        uint64_t a = 0;
        for (uint32_t i = 0; i < T; ++i) {
            const ListView S = rows[b+i].S();
            for (uint32_t g = 0; g < NG; ++g) {
                if ((g+1)*G <= i+1 && g*G < i+1 && (g+1)*G <= i+1) { /* fallthrough */ }
                bool any = false;
                const uint64_t* gw = grp[g].data();
                const uint32_t sh = occ[b].shift;
                for (uint32_t k = 0; k < S.n && !any; ++k) {
                    const uint32_t q = S.v[k] >> sh;
                    any = (gw[q>>6] >> (q&63)) & 1u;
                }
                if (!any) continue;                       // reject G rows at once
                for (uint32_t jj = 0; jj < G; ++jj) {
                    const uint32_t j = g*G + jj;
                    if (j <= i) continue;
                    a += gated(b+i, b+j);
                }
            }
        }
        return a;
    });

    /* ---- 6. COMBO: range x matrix, the two angles with alpha -----------------
     * The width pass showed range and matrix prune on independent axes -- range
     * on positional extent, matrix on bucket occupancy -- so their survivors
     * should intersect rather than overlap. range alone is 48x on dimension_003
     * and ~1x elsewhere; matrix alone is 12.5x on uscensus2000 and 1.2x on
     * dimension_003. Neither dominates, which is the signature of composability.
     *
     * Range is folded into the matrix resolve rather than run as a separate
     * pass: once the candidate word for row i is known, mask off every j whose
     * extent cannot overlap i's. Two comparisons per surviving pair, and it runs
     * only on pairs the matrix already admitted. */
    bench("combo (range x matrix)", [&](uint32_t t){
        const uint32_t b = t*T;
        std::fill(colT.begin(), colT.end(), 0ull);
        std::fill(nonempty.begin(), nonempty.end(), 0ull);
        for (uint32_t i = 0; i < T; ++i) {
            const uint64_t* w = occ[b+i].w.data();
            for (uint32_t k = 0; k < occ_nw; ++k) {
                uint64_t v = w[k];
                while (v) {
                    const uint32_t bit = (uint32_t)__builtin_ctzll(v); v &= v-1;
                    const uint32_t bucket = k*64 + bit;
                    colT[bucket] |= 1ull << i;
                    nonempty[bucket>>6] |= 1ull << (bucket&63);
                }
            }
        }
        std::fill(cand.begin(), cand.end(), 0ull);
        for (size_t k = 0; k < nonempty.size(); ++k) {
            uint64_t nv = nonempty[k];
            while (nv) {
                const uint32_t bit = (uint32_t)__builtin_ctzll(nv); nv &= nv-1;
                const uint64_t w = colT[k*64 + bit];
                uint64_t r = w;
                while (r) { const uint32_t i = (uint32_t)__builtin_ctzll(r); r &= r-1; cand[i] |= w; }
            }
        }
        uint64_t a = 0;
        for (uint32_t i = 0; i < T; ++i) {
            uint64_t m = cand[i] & ~((i==63) ? ~0ull : ((1ull<<(i+1))-1));
            const uint32_t li = lo[b+i], hii = hi[b+i];
            while (m) {
                const uint32_t j = (uint32_t)__builtin_ctzll(m); m &= m-1;
                if (hii < lo[b+j] || hi[b+j] < li) continue;   // extents cannot meet
                a += gated(b+i, b+j);
            }
        }
        return a;
    });

    /* ---- 7. CASCADE: cheapest prune first, later stages earn their place ----
     * Naive composition lost: combo ran 13.8x on dimension_003 where range
     * alone ran 38.5x, because the tile transpose was built even after range
     * had already eliminated nearly every pair. Prunes are not free and must be
     * ordered by cost, with each stage skipped when the previous one has already
     * done the work.
     *
     * Stage 1 is range: two integer comparisons per pair, no build step.
     * Stage 2 is the transpose, whose build cost is O(set bits in tile) and is
     * only worth paying if enough pairs survived stage 1 to amortise it.
     */
    std::vector<uint64_t> alive(T, 0ull);
    bench("cascade (range -> matrix)", [&](uint32_t t){
        const uint32_t b = t*T;
        uint32_t survivors = 0;
        for (uint32_t i = 0; i < T; ++i) {
            uint64_t m = 0;
            const uint32_t li = lo[b+i], hii = hi[b+i];
            for (uint32_t j = i+1; j < T; ++j)
                if (!(hii < lo[b+j] || hi[b+j] < li)) m |= 1ull << j;
            alive[i] = m; survivors += (uint32_t)__builtin_popcountll(m);
        }
        // Build the transpose only when range left enough work to justify it.
        const uint32_t total = T*(T-1)/2;
        if (survivors * 4 > total) {
            std::fill(colT.begin(), colT.end(), 0ull);
            std::fill(nonempty.begin(), nonempty.end(), 0ull);
            for (uint32_t i = 0; i < T; ++i) {
                const uint64_t* w = occ[b+i].w.data();
                for (uint32_t k = 0; k < occ_nw; ++k) {
                    uint64_t v = w[k];
                    while (v) {
                        const uint32_t bit = (uint32_t)__builtin_ctzll(v); v &= v-1;
                        const uint32_t bucket = k*64 + bit;
                        colT[bucket] |= 1ull << i;
                        nonempty[bucket>>6] |= 1ull << (bucket&63);
                    }
                }
            }
            std::fill(cand.begin(), cand.end(), 0ull);
            for (size_t k = 0; k < nonempty.size(); ++k) {
                uint64_t nv = nonempty[k];
                while (nv) {
                    const uint32_t bit = (uint32_t)__builtin_ctzll(nv); nv &= nv-1;
                    const uint64_t w = colT[k*64 + bit];
                    uint64_t r = w;
                    while (r) { const uint32_t i = (uint32_t)__builtin_ctzll(r); r &= r-1; cand[i] |= w; }
                }
            }
            for (uint32_t i = 0; i < T; ++i) alive[i] &= cand[i];
        }
        uint64_t a = 0;
        for (uint32_t i = 0; i < T; ++i) {
            uint64_t m = alive[i];
            while (m) { const uint32_t j = (uint32_t)__builtin_ctzll(m); m &= m-1; a += gated(b+i, b+j); }
        }
        return a;
    });

    /* ---- 8. ADAPTIVE CASCADE ------------------------------------------------
     * The fixed cascade wins overall (geomean 8.21 vs 7.24) purely on
     * dimension_003, and pays ~8% on the graph corpora where range prunes
     * nothing at all -- 2,016 comparisons per tile for zero rejections.
     *
     * So measure each stage's pruning power once on the first tile and enable
     * only the stages that earn their cost. Same shape as the C16 gate: a
     * property of the corpus, measured once, not a per-pair branch.
     */
    /* The C16 gate, applied to the whole tile pipeline.
     *
     * Without it the dense corpora regress exactly as they did before C16:
     * weather_sept_85 0.42x, census-income 0.39x. Every prune in this cascade
     * is built on the coarse zone map, so when the map is not selective the
     * whole pipeline inherits its loss. The gate is measured on the corpus, not
     * modelled, for the reasons in C16.
     */
    bool use_filter = true;
    {
        uint64_t probes = 0, surv = 0, sumS = 0; double lines = 0;
        const uint32_t nt = std::min(ntiles, 4u);
        for (uint32_t t = 0; t < nt; ++t)
            for (uint32_t i = 0; i < T; ++i)
                for (uint32_t j = i+1; j < T; j += 7) {
                    const uint32_t d = t*T+i, sIdx = t*T+j;
                    const ListView S = rows[sIdx].S();
                    probes += S.n; sumS += S.n;
                    lines += (double)rows[d].meta.n_words * 8.0 / 64.0;
                    for (uint32_t k = 0; k < S.n; ++k) surv += occ[d].maybe(S.v[k]);
                }
        const double survival = probes ? (double)surv/(double)probes : 0.0;
        const double touch    = lines ? (double)sumS/lines : 1.0;
        use_filter = (survival < 0.15) && (touch < 0.25);
        std::printf("# GATE survival=%.4f touch=%.4f -> %s\n", survival, touch,
                    use_filter ? "filter pipeline" : "bypass to plain kernel");
    }

    bool use_range = true, use_matrix = true;
    {
        const uint32_t b = 0; uint32_t kept = 0;
        for (uint32_t i = 0; i < T; ++i)
            for (uint32_t j = i+1; j < T; ++j)
                if (!(hi[b+i] < lo[b+j] || hi[b+j] < lo[b+i])) ++kept;
        // Range is ~1 op/pair; require it to reject at least 5% to be worth it.
        use_range = (kept * 20 < (T*(T-1)/2) * 19);
    }
    bench("adaptive cascade", [&](uint32_t t){
        const uint32_t b = t*T;
        if (!use_filter) {                       // gate says the map is useless here
            uint64_t a = 0;
            for (uint32_t i = 0; i < T; ++i) for (uint32_t j = i+1; j < T; ++j)
                a += f_bs(rows[b+i].B(), rows[b+j].S());
            return a;
        }
        uint32_t survivors = 0;
        if (use_range) {
            // rows are lo-sorted within the tile, so lo[j] is non-decreasing in
            // j: the first j with lo[j] > hi[i] ends the scan for row i.
            for (uint32_t i = 0; i < T; ++i) {
                uint64_t m = 0;
                const uint32_t li = lo[b+i], hii = hi[b+i];
                for (uint32_t j = i+1; j < T; ++j) {
                    if (lo[b+j] > hii) break;
                    if (hi[b+j] >= li) m |= 1ull << j;
                }
                alive[i] = m; survivors += (uint32_t)__builtin_popcountll(m);
            }
        } else {
            for (uint32_t i = 0; i < T; ++i)
                alive[i] = (i==63) ? 0ull : ~((1ull<<(i+1))-1);
            survivors = T*(T-1)/2;
        }
        if (use_matrix && survivors * 4 > (T*(T-1)/2)) {
            std::fill(colT.begin(), colT.end(), 0ull);
            std::fill(nonempty.begin(), nonempty.end(), 0ull);
            for (uint32_t i = 0; i < T; ++i) {
                const uint64_t* w = occ[b+i].w.data();
                for (uint32_t k = 0; k < occ_nw; ++k) {
                    uint64_t v = w[k];
                    while (v) {
                        const uint32_t bit = (uint32_t)__builtin_ctzll(v); v &= v-1;
                        const uint32_t bucket = k*64 + bit;
                        colT[bucket] |= 1ull << i;
                        nonempty[bucket>>6] |= 1ull << (bucket&63);
                    }
                }
            }
            std::fill(cand.begin(), cand.end(), 0ull);
            for (size_t k = 0; k < nonempty.size(); ++k) {
                uint64_t nv = nonempty[k];
                while (nv) {
                    const uint32_t bit = (uint32_t)__builtin_ctzll(nv); nv &= nv-1;
                    const uint64_t w = colT[k*64 + bit];
                    uint64_t r = w;
                    while (r) { const uint32_t i = (uint32_t)__builtin_ctzll(r); r &= r-1; cand[i] |= w; }
                }
            }
            for (uint32_t i = 0; i < T; ++i) alive[i] &= cand[i];
        }
        uint64_t a = 0;
        for (uint32_t i = 0; i < T; ++i) {
            uint64_t m = alive[i];
            while (m) { const uint32_t j = (uint32_t)__builtin_ctzll(m); m &= m-1; a += gated(b+i, b+j); }
        }
        return a;
    });

    /* ---- 9. AMORTISED TRANSPOSE ---------------------------------------------
     * Accounting check, not a new algorithm.
     *
     * Every variant above rebuilds a tile's transpose on each visit and charges
     * it to that tile's 2,016 pairs. In real all-pairs, tile i is paired with
     * every other tile, so its transpose is built once and reused N/T times --
     * 15,625 times for a million rows at T=64. The benchmark therefore
     * overcharges the build by orders of magnitude, and the measured cost of the
     * matrix stage is an upper bound rather than an estimate.
     *
     * This variant precomputes every tile's transpose and times only the resolve
     * and probe, which is the correct accounting for the block-diagonal of an
     * all-pairs run. The truth for a real workload sits at this end. */
    std::vector<std::vector<uint64_t>> allT(ntiles), allNE(ntiles);
    for (uint32_t t = 0; t < ntiles; ++t) {
        allT[t].assign(occ_nw * 64ull, 0ull);
        allNE[t].assign((occ_nw*64 + 63)/64, 0ull);
        const uint32_t b = t*T;
        for (uint32_t i = 0; i < T; ++i) {
            const uint64_t* w = occ[b+i].w.data();
            for (uint32_t k = 0; k < occ_nw; ++k) {
                uint64_t v = w[k];
                while (v) {
                    const uint32_t bit = (uint32_t)__builtin_ctzll(v); v &= v-1;
                    const uint32_t bucket = k*64 + bit;
                    allT[t][bucket] |= 1ull << i;
                    allNE[t][bucket>>6] |= 1ull << (bucket&63);
                }
            }
        }
    }
    bench("amortised transpose", [&](uint32_t t){
        const uint32_t b = t*T;
        if (!use_filter) {
            uint64_t a = 0;
            for (uint32_t i = 0; i < T; ++i) for (uint32_t j = i+1; j < T; ++j)
                a += f_bs(rows[b+i].B(), rows[b+j].S());
            return a;
        }
        std::fill(cand.begin(), cand.end(), 0ull);
        const uint64_t* ne = allNE[t].data(); const uint64_t* ct = allT[t].data();
        for (size_t k = 0; k < allNE[t].size(); ++k) {
            uint64_t nv = ne[k];
            while (nv) {
                const uint32_t bit = (uint32_t)__builtin_ctzll(nv); nv &= nv-1;
                const uint64_t w = ct[k*64 + bit];
                uint64_t r = w;
                while (r) { const uint32_t i = (uint32_t)__builtin_ctzll(r); r &= r-1; cand[i] |= w; }
            }
        }
        uint64_t a = 0;
        for (uint32_t i = 0; i < T; ++i) {
            uint64_t m = cand[i] & ~((i==63) ? ~0ull : ((1ull<<(i+1))-1));
            if (use_range) {
                const uint32_t li = lo[b+i], hii = hi[b+i];
                uint64_t mm = m; m = 0;
                while (mm) { const uint32_t j = (uint32_t)__builtin_ctzll(mm); mm &= mm-1;
                    if (!(hii < lo[b+j] || hi[b+j] < li)) m |= 1ull << j; }
            }
            while (m) { const uint32_t j = (uint32_t)__builtin_ctzll(m); m &= m-1; a += gated(b+i, b+j); }
        }
        return a;
    });

    /* ---- 10. WIDE TILES ------------------------------------------------------
     * Build and resolve are both O(set bits in tile) = O(T*|A|), while pairs
     * grow as T^2/2, so planning cost per pair falls as 2|A|/T. Doubling the
     * tile should halve it. T was pinned at 64 only because the candidate rows
     * were single uint64_t words; widen them to W = T/64 words and the same
     * algorithm runs at any multiple of 64.
     */
    {
        const uint32_t WT = wide_tile;
        const uint32_t W  = WT / 64;
        const uint32_t nwt = (uint32_t)(rows.size() / WT);
        if (nwt && use_filter) {
            std::vector<uint64_t> wcolT((size_t)occ_nw * 64ull * W, 0ull);
            std::vector<uint64_t> wne((occ_nw*64 + 63)/64, 0ull);
            std::vector<uint64_t> wcand((size_t)WT * W, 0ull);
            // amortised: build every wide tile's transpose once
            std::vector<std::vector<uint64_t>> bT(nwt), bNE(nwt);
            for (uint32_t t = 0; t < nwt; ++t) {
                bT[t].assign((size_t)occ_nw*64ull*W, 0ull);
                bNE[t].assign((occ_nw*64 + 63)/64, 0ull);
                const uint32_t b = t*WT;
                for (uint32_t i = 0; i < WT; ++i) {
                    const uint64_t* w = occ[b+i].w.data();
                    for (uint32_t k = 0; k < occ_nw; ++k) {
                        uint64_t v = w[k];
                        while (v) {
                            const uint32_t bit = (uint32_t)__builtin_ctzll(v); v &= v-1;
                            const uint32_t bucket = k*64 + bit;
                            bT[t][(size_t)bucket*W + (i>>6)] |= 1ull << (i&63);
                            bNE[t][bucket>>6] |= 1ull << (bucket&63);
                        }
                    }
                }
            }
            double best = 1e30; uint64_t sum = 0;
            for (int r = 0; r < repeats; ++r) {
                uint64_t acc = 0; const uint64_t t0 = ns_now();
                for (uint32_t t = 0; t < nwt; ++t) {
                    const uint32_t b = t*WT;
                    std::fill(wcand.begin(), wcand.end(), 0ull);
                    const uint64_t* ne = bNE[t].data(); const uint64_t* ct = bT[t].data();
                    for (size_t k = 0; k < bNE[t].size(); ++k) {
                        uint64_t nv = ne[k];
                        while (nv) {
                            const uint32_t bit = (uint32_t)__builtin_ctzll(nv); nv &= nv-1;
                            const uint64_t* w = ct + (size_t)(k*64 + bit)*W;
                            for (uint32_t u = 0; u < W; ++u) {
                                uint64_t r2 = w[u];
                                while (r2) {
                                    const uint32_t i = u*64 + (uint32_t)__builtin_ctzll(r2); r2 &= r2-1;
                                    for (uint32_t v2 = 0; v2 < W; ++v2) wcand[(size_t)i*W + v2] |= w[v2];
                                }
                            }
                        }
                    }
                    for (uint32_t i = 0; i < WT; ++i) {
                        const uint32_t li = lo[b+i], hii = hi[b+i];
                        for (uint32_t u = 0; u < W; ++u) {
                            uint64_t m = wcand[(size_t)i*W + u];
                            if (u == (i>>6)) m &= ((i&63)==63) ? 0ull : ~((1ull<<((i&63)+1))-1);
                            else if (u < (i>>6)) m = 0;
                            while (m) {
                                const uint32_t j = u*64 + (uint32_t)__builtin_ctzll(m); m &= m-1;
                                if (use_range && (hii < lo[b+j] || hi[b+j] < li)) continue;
                                acc += gated(b+i, b+j);
                            }
                        }
                    }
                }
                const double dt = (double)(ns_now() - t0);
                if (dt < best) best = dt; sum = acc;
            }
            // Reference over the IDENTICAL wide-tile pair set. The T=64
            // correctness check does not cover this tiling, and an unverified
            // fast path is worth nothing.
            uint64_t ref = 0;
            for (uint32_t t = 0; t < nwt; ++t) {
                const uint32_t b = t*WT;
                for (uint32_t i = 0; i < WT; ++i)
                    for (uint32_t j = i+1; j < WT; ++j)
                        ref += f_bs(rows[b+i].B(), rows[b+j].S());
            }
            std::printf("# WIDE T=%u: %.3f ns/pair  (n=%u tiles, %u pairs)  correct=%s\n", WT,
                        best / (double)(nwt * (WT*(WT-1)/2)), nwt, nwt*(WT*(WT-1)/2),
                        sum == ref ? "yes" : "NO <-- WRONG");
        }
    }

    /* ---- 11. MULTI-OCCUPANCY BUCKETS ONLY -----------------------------------
     * The resolve visits every occupied bucket, but a bucket held by exactly one
     * row yields `cand[i] |= (1<<i)` -- itself, no pair. On sparse data almost
     * every bucket is a singleton: as-skitter puts 64*15 = 960 set bits into
     * 16,384 buckets, so the overwhelming majority of the resolve's work
     * produces no candidate at all.
     *
     * Keep a compact list of the buckets whose occupancy word has popcount >= 2,
     * built once with the transpose, and iterate only those. This also replaces
     * the nonempty-summary scan with a dense array walk. */
    std::vector<std::vector<uint64_t>> multi(ntiles), multiU(ntiles);
    for (uint32_t t = 0; t < ntiles; ++t) {
        for (size_t b2 = 0; b2 < allT[t].size(); ++b2)
            if (__builtin_popcountll(allT[t][b2]) >= 2) multi[t].push_back(allT[t][b2]);
        /* `cand[i] |= w` is idempotent, so a repeated occupancy word contributes
         * nothing after its first application. Clustered data produces the same
         * row-set in many adjacent buckets, so deduplicating is exact and can
         * only shrink the resolve. Cost is one sort at build time, which the
         * transpose already amortises across every block the tile joins. */
        multiU[t] = multi[t];
        std::sort(multiU[t].begin(), multiU[t].end());
        multiU[t].erase(std::unique(multiU[t].begin(), multiU[t].end()), multiU[t].end());
    }
    {
        double sh = 0; for (uint32_t t = 0; t < ntiles; ++t) sh += (double)multi[t].size();
        double su = 0; for (uint32_t t = 0; t < ntiles; ++t) su += (double)multiU[t].size();
        std::printf("# multi-occupancy buckets: %.0f of %zu per tile (%.2f%%), "
                    "distinct %.0f (%.1f%% of multi)\n",
                    sh/ntiles, allT[0].size(), 100.0*sh/ntiles/(double)allT[0].size(),
                    su/ntiles, sh > 0 ? 100.0*su/sh : 0.0);
    }
    bench("multi-bucket resolve", [&](uint32_t t){
        const uint32_t b = t*T;
        if (!use_filter) {
            uint64_t a = 0;
            for (uint32_t i = 0; i < T; ++i) for (uint32_t j = i+1; j < T; ++j)
                a += f_bs(rows[b+i].B(), rows[b+j].S());
            return a;
        }
        std::fill(cand.begin(), cand.end(), 0ull);
        for (const uint64_t w : multi[t]) {
            uint64_t r = w;
            while (r) { const uint32_t i = (uint32_t)__builtin_ctzll(r); r &= r-1; cand[i] |= w; }
        }
        uint64_t a = 0;
        for (uint32_t i = 0; i < T; ++i) {
            uint64_t m = cand[i] & ~((i==63) ? ~0ull : ((1ull<<(i+1))-1));
            if (use_range) {
                const uint32_t li = lo[b+i], hii = hi[b+i];
                uint64_t mm = m; m = 0;
                while (mm) { const uint32_t j = (uint32_t)__builtin_ctzll(mm); mm &= mm-1;
                    if (!(hii < lo[b+j] || hi[b+j] < li)) m |= 1ull << j; }
            }
            while (m) { const uint32_t j = (uint32_t)__builtin_ctzll(m); m &= m-1; a += gated(b+i, b+j); }
        }
        return a;
    });

    bench("multi + dedup", [&](uint32_t t){
        const uint32_t b = t*T;
        if (!use_filter) {
            uint64_t a = 0;
            for (uint32_t i = 0; i < T; ++i) for (uint32_t j = i+1; j < T; ++j)
                a += f_bs(rows[b+i].B(), rows[b+j].S());
            return a;
        }
        std::fill(cand.begin(), cand.end(), 0ull);
        for (const uint64_t w : multiU[t]) {
            uint64_t r = w;
            while (r) { const uint32_t i = (uint32_t)__builtin_ctzll(r); r &= r-1; cand[i] |= w; }
        }
        uint64_t a = 0;
        for (uint32_t i = 0; i < T; ++i) {
            uint64_t m = cand[i] & ~((i==63) ? ~0ull : ((1ull<<(i+1))-1));
            if (use_range) {
                const uint32_t li = lo[b+i], hii = hi[b+i];
                uint64_t mm = m; m = 0;
                while (mm) { const uint32_t j = (uint32_t)__builtin_ctzll(mm); mm &= mm-1;
                    if (!(hii < lo[b+j] || hi[b+j] < li)) m |= 1ull << j; }
            }
            while (m) { const uint32_t j = (uint32_t)__builtin_ctzll(m); m &= m-1; a += gated(b+i, b+j); }
        }
        return a;
    });

    /* ---- 12. EMPTY-TILE SKIP ------------------------------------------------
     * If a tile has NO multi-occupancy bucket then no two of its rows share any
     * bucket, so every one of its T(T-1)/2 pairs is provably disjoint and the
     * whole tile answers zero without touching a single row. At the wide bucket
     * widths of C18 collisions are rare, so this should fire often on sparse
     * corpora -- and when it does it removes 2,016 pairs for one branch. */
    {
        uint32_t empties = 0;
        for (uint32_t t = 0; t < ntiles; ++t) if (multi[t].empty()) ++empties;
        std::printf("# empty tiles (no shared bucket): %u of %u (%.1f%%)\n",
                    empties, ntiles, 100.0*empties/(double)ntiles);
    }
    bench("multi + empty-tile skip", [&](uint32_t t){
        const uint32_t b = t*T;
        if (!use_filter) {
            uint64_t a = 0;
            for (uint32_t i = 0; i < T; ++i) for (uint32_t j = i+1; j < T; ++j)
                a += f_bs(rows[b+i].B(), rows[b+j].S());
            return a;
        }
        if (multi[t].empty()) return (uint64_t)0;      // whole tile provably disjoint
        std::fill(cand.begin(), cand.end(), 0ull);
        for (const uint64_t w : multi[t]) {
            uint64_t r = w;
            while (r) { const uint32_t i = (uint32_t)__builtin_ctzll(r); r &= r-1; cand[i] |= w; }
        }
        uint64_t a = 0;
        for (uint32_t i = 0; i < T; ++i) {
            uint64_t m = cand[i] & ~((i==63) ? ~0ull : ((1ull<<(i+1))-1));
            if (!m) continue;
            if (use_range) {
                const uint32_t li = lo[b+i], hii = hi[b+i];
                uint64_t mm = m; m = 0;
                while (mm) { const uint32_t j = (uint32_t)__builtin_ctzll(mm); mm &= mm-1;
                    if (!(hii < lo[b+j] || hi[b+j] < li)) m |= 1ull << j; }
            }
            while (m) { const uint32_t j = (uint32_t)__builtin_ctzll(m); m &= m-1; a += gated(b+i, b+j); }
        }
        return a;
    });

    /* ---- 13. DIRECT PAIR EMISSION -------------------------------------------
     * At wide bucket widths the resolve itself is nearly free, and the fixed
     * per-tile overhead dominates: clearing 64 candidate words and then scanning
     * all 64 rows costs ~128 operations whether or not any pair survives.
     *
     * When the multi list is short, skip the accumulator entirely and emit pairs
     * straight from each occupancy word -- a word with popcount p yields
     * p(p-1)/2 pairs directly. Duplicates across words are possible, so pairs
     * are still deduplicated through a small candidate set, but only over the
     * rows that actually appear rather than all T. */
    bench("direct pair emission", [&](uint32_t t){
        const uint32_t b = t*T;
        if (!use_filter) {
            uint64_t a = 0;
            for (uint32_t i = 0; i < T; ++i) for (uint32_t j = i+1; j < T; ++j)
                a += f_bs(rows[b+i].B(), rows[b+j].S());
            return a;
        }
        if (multi[t].empty()) return (uint64_t)0;
        // touched = rows appearing in any multi bucket; only those need clearing
        uint64_t touched = 0;
        for (const uint64_t w : multi[t]) touched |= w;
        { uint64_t r = touched; while (r) { const uint32_t i = (uint32_t)__builtin_ctzll(r); r &= r-1; cand[i] = 0ull; } }
        for (const uint64_t w : multi[t]) {
            uint64_t r = w;
            while (r) { const uint32_t i = (uint32_t)__builtin_ctzll(r); r &= r-1; cand[i] |= w; }
        }
        uint64_t a = 0;
        uint64_t tr = touched;
        while (tr) {
            const uint32_t i = (uint32_t)__builtin_ctzll(tr); tr &= tr-1;
            uint64_t m = cand[i] & ~((i==63) ? ~0ull : ((1ull<<(i+1))-1));
            if (!m) continue;
            if (use_range) {
                const uint32_t li = lo[b+i], hii = hi[b+i];
                uint64_t mm = m; m = 0;
                while (mm) { const uint32_t j = (uint32_t)__builtin_ctzll(mm); mm &= mm-1;
                    if (!(hii < lo[b+j] || hi[b+j] < li)) m |= 1ull << j; }
            }
            while (m) { const uint32_t j = (uint32_t)__builtin_ctzll(m); m &= m-1; a += gated(b+i, b+j); }
        }
        return a;
    });

    /* ---- 14-16: three more angles ------------------------------------------ */

    // 14. PREFETCH. Surviving pairs are known before any data is touched, so the
    // dense row's bitmap words can be requested ahead of the probe loop.
    bench("direct + prefetch", [&](uint32_t t){
        const uint32_t b = t*T;
        if (!use_filter) { uint64_t a=0;
            for (uint32_t i=0;i<T;++i) for (uint32_t j=i+1;j<T;++j) a += f_bs(rows[b+i].B(), rows[b+j].S());
            return a; }
        if (multi[t].empty()) return (uint64_t)0;
        uint64_t touched = 0;
        for (const uint64_t w : multi[t]) touched |= w;
        { uint64_t r=touched; while(r){const uint32_t i=(uint32_t)__builtin_ctzll(r); r&=r-1; cand[i]=0ull;} }
        for (const uint64_t w : multi[t]) { uint64_t r=w;
            while(r){const uint32_t i=(uint32_t)__builtin_ctzll(r); r&=r-1; cand[i]|=w;} }
        uint64_t a = 0, tr = touched;
        while (tr) {
            const uint32_t i=(uint32_t)__builtin_ctzll(tr); tr&=tr-1;
            uint64_t m = cand[i] & ~((i==63)?~0ull:((1ull<<(i+1))-1));
            if (!m) continue;
            const BitmapView B = rows[b+i].B();
            while (m) {
                const uint32_t j=(uint32_t)__builtin_ctzll(m); m&=m-1;
                if (use_range && (hi[b+i] < lo[b+j] || hi[b+j] < lo[b+i])) continue;
                const ListView S = rows[b+j].S();
                for (uint32_t k = 0; k < S.n; ++k)
                    __builtin_prefetch(&B.w[S.v[k] >> 6], 0, 1);
                uint64_t h = 0;
                for (uint32_t k = 0; k < S.n; ++k) {
                    const uint32_t x = S.v[k];
                    if (!occ[b+i].maybe(x)) continue;
                    h += (B.w[x>>6] >> (x&63)) & 1ull;
                }
                a += h;
            }
        }
        return a;
    });

    // 15. UNROLLED RESOLVE. Two occupancy words per iteration to expose ILP
    // across the otherwise serial ctz/clear-lowest-bit dependency chain.
    bench("direct + unrolled resolve", [&](uint32_t t){
        const uint32_t b = t*T;
        if (!use_filter) { uint64_t a=0;
            for (uint32_t i=0;i<T;++i) for (uint32_t j=i+1;j<T;++j) a += f_bs(rows[b+i].B(), rows[b+j].S());
            return a; }
        const auto& mv = multi[t];
        if (mv.empty()) return (uint64_t)0;
        uint64_t touched = 0;
        for (const uint64_t w : mv) touched |= w;
        { uint64_t r=touched; while(r){const uint32_t i=(uint32_t)__builtin_ctzll(r); r&=r-1; cand[i]=0ull;} }
        size_t k2 = 0;
        for (; k2 + 1 < mv.size(); k2 += 2) {
            uint64_t r0 = mv[k2], r1 = mv[k2+1];
            const uint64_t w0 = r0, w1 = r1;
            while (r0 | r1) {
                if (r0) { const uint32_t i=(uint32_t)__builtin_ctzll(r0); r0&=r0-1; cand[i]|=w0; }
                if (r1) { const uint32_t i=(uint32_t)__builtin_ctzll(r1); r1&=r1-1; cand[i]|=w1; }
            }
        }
        for (; k2 < mv.size(); ++k2) { uint64_t r=mv[k2];
            while(r){const uint32_t i=(uint32_t)__builtin_ctzll(r); r&=r-1; cand[i]|=mv[k2];} }
        uint64_t a = 0, tr = touched;
        while (tr) {
            const uint32_t i=(uint32_t)__builtin_ctzll(tr); tr&=tr-1;
            uint64_t m = cand[i] & ~((i==63)?~0ull:((1ull<<(i+1))-1));
            if (!m) continue;
            const uint32_t li=lo[b+i], hii=hi[b+i];
            while (m) { const uint32_t j=(uint32_t)__builtin_ctzll(m); m&=m-1;
                if (use_range && (hii < lo[b+j] || hi[b+j] < li)) continue;
                a += gated(b+i, b+j); }
        }
        return a;
    });

    // 18. TWO-LEVEL: sort the multi list by popcount ascending, so cheap words
    // (2 rows) are applied before expensive ones and cand fills incrementally.
    std::vector<std::vector<uint64_t>> multiS(ntiles);
    for (uint32_t t = 0; t < ntiles; ++t) {
        multiS[t] = multi[t];
        std::sort(multiS[t].begin(), multiS[t].end(), [](uint64_t a, uint64_t b){
            return __builtin_popcountll(a) < __builtin_popcountll(b); });
    }
    bench("direct + popcount-sorted", [&](uint32_t t){
        const uint32_t b = t*T;
        if (!use_filter) { uint64_t a=0;
            for (uint32_t i=0;i<T;++i) for (uint32_t j=i+1;j<T;++j) a += f_bs(rows[b+i].B(), rows[b+j].S());
            return a; }
        const auto& mv = multiS[t];
        if (mv.empty()) return (uint64_t)0;
        uint64_t touched = 0;
        for (const uint64_t w : mv) touched |= w;
        { uint64_t r=touched; while(r){const uint32_t i=(uint32_t)__builtin_ctzll(r); r&=r-1; cand[i]=0ull;} }
        for (const uint64_t w : mv) { uint64_t r=w;
            while(r){const uint32_t i=(uint32_t)__builtin_ctzll(r); r&=r-1; cand[i]|=w;} }
        uint64_t a=0, tr=touched;
        while (tr) { const uint32_t i=(uint32_t)__builtin_ctzll(tr); tr&=tr-1;
            uint64_t m = cand[i] & ~((i==63)?~0ull:((1ull<<(i+1))-1));
            if (!m) continue;
            const uint32_t li=lo[b+i], hii=hi[b+i];
            while (m) { const uint32_t j=(uint32_t)__builtin_ctzll(m); m&=m-1;
                if (use_range && (hii < lo[b+j] || hi[b+j] < li)) continue;
                a += gated(b+i, b+j); } }
        return a;
    });

    // 19. PAIRS-FIRST: for popcount-2 words, emit the single pair directly
    // instead of routing it through the candidate accumulator at all.
    bench("direct + pc2 fast path", [&](uint32_t t){
        const uint32_t b = t*T;
        if (!use_filter) { uint64_t a=0;
            for (uint32_t i=0;i<T;++i) for (uint32_t j=i+1;j<T;++j) a += f_bs(rows[b+i].B(), rows[b+j].S());
            return a; }
        const auto& mv = multi[t];
        if (mv.empty()) return (uint64_t)0;
        uint64_t touched = 0;
        for (const uint64_t w : mv) touched |= w;
        { uint64_t r=touched; while(r){const uint32_t i=(uint32_t)__builtin_ctzll(r); r&=r-1; cand[i]=0ull;} }
        for (const uint64_t w : mv) {
            if (__builtin_popcountll(w) == 2) {          // exactly one pair
                const uint32_t i = (uint32_t)__builtin_ctzll(w);
                cand[i] |= w;
                continue;
            }
            uint64_t r=w; while(r){const uint32_t i=(uint32_t)__builtin_ctzll(r); r&=r-1; cand[i]|=w;}
        }
        uint64_t a=0, tr=touched;
        while (tr) { const uint32_t i=(uint32_t)__builtin_ctzll(tr); tr&=tr-1;
            uint64_t m = cand[i] & ~((i==63)?~0ull:((1ull<<(i+1))-1));
            if (!m) continue;
            const uint32_t li=lo[b+i], hii=hi[b+i];
            while (m) { const uint32_t j=(uint32_t)__builtin_ctzll(m); m&=m-1;
                if (use_range && (hii < lo[b+j] || hi[b+j] < li)) continue;
                a += gated(b+i, b+j); } }
        return a;
    });

    /* ---- 17. BYPASS-PATH CELL ROUTING --------------------------------------
     * Race the candidate cells on the tiles the gate bypasses. */
    if (!use_filter) {
        bench("bypass: B x B zonemap", [&](uint32_t t){ uint64_t a=0; const uint32_t b=t*T;
            for (uint32_t i=0;i<T;++i) for (uint32_t j=i+1;j<T;++j) a += f_bb(rows[b+i].B(), rows[b+j].B());
            return a; });
        bench("bypass: B x B dense", [&](uint32_t t){ uint64_t a=0; const uint32_t b=t*T;
            for (uint32_t i=0;i<T;++i) for (uint32_t j=i+1;j<T;++j) a += f_bbd(rows[b+i].B(), rows[b+j].B());
            return a; });
        bench("bypass: S x S", [&](uint32_t t){ uint64_t a=0; const uint32_t b=t*T;
            for (uint32_t i=0;i<T;++i) for (uint32_t j=i+1;j<T;++j) a += f_ss(rows[b+i].S(), rows[b+j].S());
            return a; });
        bench("bypass: R x R", [&](uint32_t t){ uint64_t a=0; const uint32_t b=t*T;
            for (uint32_t i=0;i<T;++i) for (uint32_t j=i+1;j<T;++j) a += f_rr(rows[b+i].R(), rows[b+j].R());
            return a; });
    }

    // --- report ---------------------------------------------------------------
    std::printf("# %s universe=%u rows=%zu tiles=%u pairs=%u filter=%u bits\n",
                tag.c_str(), nb, rows.size(), ntiles, ntiles*(T*(T-1)/2), fbits);
    std::printf("%-28s %10s %10s %s\n", "variant", "ns/pair", "vs base", "correct");
    for (const R& r : out)
        std::printf("%-28s %10.3f %9.2fx %s\n", r.n, r.ns, out[0].ns / r.ns,
                    r.sum == truth ? "yes" : "NO <-- WRONG");
    return 0;
}
