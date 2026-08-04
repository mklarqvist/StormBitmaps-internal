/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist. Apache-2.0.
 */
#include "kernels/storm_allpairs.h"
#include "kernels/storm_cells.h"

#include <algorithm>
#include <numeric>
#include <time.h>

namespace storm {

const char* name_of(Policy p) {
    switch (p) {
        case Policy::AllBitmap: return "all-bitmap";
        case Policy::PerPair:   return "per-pair";
        case Policy::PerTile:   return "per-tile";
        case Policy::Oracle:    return "oracle";
    }
    return "?";
}

namespace {

inline uint64_t now_ns() {
#if defined(__APPLE__)
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
#endif
}

template <class L>
auto pick(L list, const char* nm) -> decltype(list.v[0].fn) {
    for (size_t i = 0; i < list.n; ++i) {
        const char* n = list.v[i].name;
        size_t k = 0;
        while (n[k] && nm[k] && n[k] == nm[k]) ++k;
        if (n[k] == '\0' && nm[k] == '\0') return list.v[i].fn;
    }
    return list.v[0].fn;
}

struct Kernels {
    fn_bb bb; fn_bs bs; fn_br br; fn_bw bw;
    fn_ss ss; fn_sr sr; fn_rr rr; fn_ww ww;
    Kernels()
        : bb(pick(cell_bb(), "occ_sel")), bs(pick(cell_bs(), "ilp8")),
          br(pick(cell_br(), "hybrid4")), bw(pick(cell_bw(), "skip")),
          ss(pick(cell_ss(), "adaptive2")), sr(pick(cell_sr(), "adaptive2")),
          rr(pick(cell_rr(), "adaptive2")), ww(pick(cell_ww(), "skip2")) {}
};

inline uint64_t run(const Kernels& K, Pairing p, const Row& d, const Row& s) {
    switch (p) {
        case Pairing::BB: return K.bb(d.B(), s.B());
        case Pairing::BS: return K.bs(d.B(), s.S());
        case Pairing::BR: return K.br(d.B(), s.R());
        case Pairing::BW: return K.bw(d.B(), s.W());
        case Pairing::SS: return K.ss(d.S(), s.S());
        case Pairing::SR: return K.sr(s.S(), d.R());
        case Pairing::RR: return K.rr(d.R(), s.R());
        case Pairing::WW: return K.ww(d.W(), s.W());
        default:          return 0;
    }
}

/* Tile-aggregate metadata (M3).
 *
 * The tile's decision has to stand in for every pair inside it, so the
 * aggregate must be conservative in the direction that matters: using the MAX
 * cardinality and MAX run count means the tile is costed as if every row were
 * its densest member. Underestimating would pick a kernel that is cheap for the
 * average row and catastrophic for the worst one, and at N^2 the worst one is
 * hit TR*TC times.
 *
 * first_set/last_set are aggregated the other way -- min of firsts, max of
 * lasts -- so the span test stays sound: a tile is only declared disjoint when
 * every pair in it certainly is. */
RowMeta tile_meta(const std::vector<Row>& rows, const std::vector<uint32_t>& order,
                  uint32_t lo, uint32_t hi)
{
    RowMeta m;
    m.first_set = UINT32_MAX;
    m.last_set  = 0;
    for (uint32_t k = lo; k < hi; ++k) {
        const RowMeta& r = rows[order[k]].meta;
        m.cardinality = std::max(m.cardinality, r.cardinality);
        m.n_runs      = std::max(m.n_runs,      r.n_runs);
        m.n_words     = std::max(m.n_words,     r.n_words);
        m.n_nonzero_w = std::max(m.n_nonzero_w, r.n_nonzero_w);
        if (r.cardinality) {
            m.first_set = std::min(m.first_set, r.first_set);
            m.last_set  = std::max(m.last_set,  r.last_set);
        }
    }
    if (m.first_set == UINT32_MAX) m.first_set = 0;
    return m;
}

// Density-sorted order (RESEARCH_PLAN.md 5.2). Rows of similar cardinality land
// in the same tile, which is what makes one decision per tile defensible.
std::vector<uint32_t> density_order(const std::vector<Row>& rows) {
    std::vector<uint32_t> o(rows.size());
    std::iota(o.begin(), o.end(), 0u);
    std::stable_sort(o.begin(), o.end(), [&](uint32_t a, uint32_t b) {
        return rows[a].meta.cardinality < rows[b].meta.cardinality;
    });
    return o;
}

} // namespace

AllPairsStats allpairs_sum(const std::vector<Row>& rows, const CostModel& model,
                           Policy policy, uint32_t tile)
{
    AllPairsStats st;
    if (rows.size() < 2) return st;
    const Kernels K;
    const uint32_t n = (uint32_t)rows.size();

    // The sort is part of the cost and is timed with everything else
    // (RESEARCH_PLAN.md 5.2: "costs to account for honestly: the sort itself").
    const uint64_t t0 = now_ns();
    const std::vector<uint32_t> ord =
        (policy == Policy::PerTile) ? density_order(rows) : [&]{
            std::vector<uint32_t> o(n); std::iota(o.begin(), o.end(), 0u); return o; }();

    double sel_ns = 0;
    uint64_t sum = 0, pairs = 0, decisions = 0, skipped = 0;

    for (uint32_t i0 = 0; i0 < n; i0 += tile) {
        const uint32_t i1 = std::min(i0 + tile, n);
        for (uint32_t j0 = i0; j0 < n; j0 += tile) {
            const uint32_t j1 = std::min(j0 + tile, n);

            Pairing tp = Pairing::BB;
            if (policy == Policy::PerTile) {
                const uint64_t s0 = now_ns();
                const RowMeta a = tile_meta(rows, ord, i0, i1);
                const RowMeta b = tile_meta(rows, ord, j0, j1);
                tp = select_pairing(model, a, b);
                if (tp == Pairing::Empty) tp = Pairing::BB;   // never skip a whole tile
                sel_ns += (double)(now_ns() - s0);
                ++decisions;
            }

            for (uint32_t i = i0; i < i1; ++i) {
                const uint32_t js = (j0 == i0) ? i + 1 : j0;
                const Row& ri = rows[ord[i]];
                for (uint32_t j = js; j < j1; ++j) {
                    const Row& rj = rows[ord[j]];
                    const bool i_dense = ri.meta.cardinality >= rj.meta.cardinality;
                    const Row& d = i_dense ? ri : rj;
                    const Row& s = i_dense ? rj : ri;
                    ++pairs;

                    Pairing p = tp;
                    if (policy == Policy::AllBitmap) {
                        p = Pairing::BB;
                    } else if (policy == Policy::PerPair) {
                        const uint64_t s0 = now_ns();
                        p = select_pairing(model, d.meta, s.meta);
                        sel_ns += (double)(now_ns() - s0);
                        ++decisions;
                    } else if (policy == Policy::Oracle) {
                        // Cheapest cell by predicted cost is NOT the oracle; the
                        // oracle must time them. Approximated here by the model's
                        // choice with a zero-cost decision, which is the upper
                        // bound a perfect free selector could reach.
                        p = select_pairing(model, d.meta, s.meta);
                    }
                    if (p == Pairing::Empty) { ++skipped; continue; }
                    sum += run(K, p, d, s);
                }
            }
        }
    }
    st.ns_total     = (double)(now_ns() - t0);
    st.ns_selection = sel_ns;
    st.sum = sum; st.pairs = pairs; st.decisions = decisions; st.skipped = skipped;
    return st;
}

AllPairsStats allpairs_tiles(const std::vector<Row>& rows, const CostModel& model,
                             Policy policy, TileCallback cb, void* ctx, uint32_t tile)
{
    AllPairsStats st;
    if (rows.size() < 2 || !cb) return st;
    const Kernels K;
    const uint32_t n = (uint32_t)rows.size();
    std::vector<uint32_t> counts((size_t)tile * tile);

    const uint64_t t0 = now_ns();
    std::vector<uint32_t> ord(n);
    std::iota(ord.begin(), ord.end(), 0u);
    if (policy == Policy::PerTile) ord = density_order(rows);

    double sel_ns = 0;
    uint64_t sum = 0, pairs = 0, decisions = 0;

    for (uint32_t i0 = 0; i0 < n; i0 += tile) {
        const uint32_t i1 = std::min(i0 + tile, n);
        for (uint32_t j0 = i0; j0 < n; j0 += tile) {
            const uint32_t j1 = std::min(j0 + tile, n);
            Pairing tp = Pairing::BB;
            if (policy == Policy::PerTile) {
                const uint64_t s0 = now_ns();
                tp = select_pairing(model, tile_meta(rows, ord, i0, i1),
                                           tile_meta(rows, ord, j0, j1));
                if (tp == Pairing::Empty) tp = Pairing::BB;
                sel_ns += (double)(now_ns() - s0);
                ++decisions;
            }
            std::fill(counts.begin(), counts.end(), 0u);
            for (uint32_t i = i0; i < i1; ++i) {
                const uint32_t js = (j0 == i0) ? i + 1 : j0;
                const Row& ri = rows[ord[i]];
                for (uint32_t j = js; j < j1; ++j) {
                    const Row& rj = rows[ord[j]];
                    const bool i_dense = ri.meta.cardinality >= rj.meta.cardinality;
                    Pairing p = (policy == Policy::AllBitmap) ? Pairing::BB : tp;
                    if (policy == Policy::PerPair || policy == Policy::Oracle)
                        p = select_pairing(model, (i_dense ? ri : rj).meta,
                                                  (i_dense ? rj : ri).meta);
                    const uint64_t v = (p == Pairing::Empty) ? 0
                                     : run(K, p, i_dense ? ri : rj, i_dense ? rj : ri);
                    counts[(size_t)(i - i0) * tile + (j - j0)] = (uint32_t)v;
                    sum += v;
                    ++pairs;
                }
            }
            cb(i0, j0, i1 - i0, j1 - j0, counts.data(), ctx);
        }
    }
    st.ns_total = (double)(now_ns() - t0);
    st.ns_selection = sel_ns;
    st.sum = sum; st.pairs = pairs; st.decisions = decisions;
    return st;
}

} // namespace storm
