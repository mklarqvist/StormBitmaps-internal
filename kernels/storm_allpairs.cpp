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
        case Policy::Fixed:     return "fixed-cell";
        case Policy::Probe:     return "probe";
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
    /* TWO B x B kernels, deliberately.
     *
     * `bb` is what the SELECTOR runs when it chooses Pairing::BB -- the good
     * zone-mapped one. `bb_plain` is the unfiltered baseline that
     * Policy::AllBitmap must use, because "all-bitmap" means what a system
     * without any of this machinery would do.
     *
     * These were the same kernel until now, so Policy::AllBitmap was silently
     * zone-mapped and every "vs all-bitmap" ratio from allpairs_sum() was
     * measured against a filtered baseline. On enwiki-categorylinks that made
     * the baseline read 649 ns/pair where a real B x B moves 32 MB per pair and
     * takes ~506,000 ns -- a 780x understatement of the baseline, and therefore
     * of every speedup derived from it. */
    fn_bb bb; fn_bb bb_plain; fn_bs bs; fn_br br; fn_bw bw;
    fn_ss ss; fn_sr sr; fn_rr rr; fn_ww ww;
    Kernels()
        : bb(pick(cell_bb(), "occ_sel")), bb_plain(pick(cell_bb(), "dense")),
          bs(pick(cell_bs(), "ilp8")),
          br(pick(cell_br(), "hybrid4")), bw(pick(cell_bw(), "skip")),
          ss(pick(cell_ss(), "adaptive2")), sr(pick(cell_sr(), "adaptive2")),
          rr(pick(cell_rr(), "adaptive2")), ww(pick(cell_ww(), "skip2")) {}
};

inline uint64_t run(const Kernels& K, Pairing p, const Row& d, const Row& s,
                    bool plain_bb = false) {
    switch (p) {
        case Pairing::BB: return plain_bb ? K.bb_plain(d.B(), s.B())
                                          : K.bb(d.B(), s.B());
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
 * MEAN cardinality and run count, not max.
 *
 * This was max, on the argument that the aggregate should be conservative:
 * underestimating picks a kernel cheap for the average row and catastrophic for
 * the worst, and at N^2 the worst is hit TR*TC times. That argument does not
 * survive contact with a power-law corpus, and it is wrong about which
 * direction is conservative in THIS matrix.
 *
 * B x B costs Theta(m) regardless of density -- it is never catastrophic and
 * never cheap. The sparse cells are cheap when the pair is sparse and degrade
 * gracefully when it is not. So over-estimating a tile does not buy safety; it
 * buys Theta(m) on the 99% of pairs that are sparse in order to avoid a small
 * constant factor on the few that are not. On as-skitter, whose degree
 * distribution spans three orders of magnitude, the densest tile's max is ~35k
 * against a median near 500, the tile therefore selected B x B, and that one
 * tile -- 6.2% of the pairs -- dragged the whole corpus from 22 ns/pair to 291.
 *
 * The mean is not merely less pessimistic, it is the right statistic. What the
 * tile decision should minimise is the tile's TOTAL cost, sum over pairs of
 * cost(p, a_i, b_j); predict() is linear in work_units, so that sum is
 * TR*TC*cost(p, mean_a, mean_b) up to the min/max terms inside work_units.
 * Deciding on the mean minimises the quantity that is actually being paid.
 *
 * first_set/last_set stay min-of-firsts and max-of-lasts, which is not a
 * cost estimate but a soundness condition: the span test may only declare a
 * tile disjoint when every pair in it certainly is. */
RowMeta tile_meta(const std::vector<Row>& rows, const std::vector<uint32_t>& order,
                  uint32_t lo, uint32_t hi)
{
    RowMeta m;
    m.first_set = UINT32_MAX;
    m.last_set  = 0;
    uint64_t card = 0, runs = 0, nnz = 0;
    const uint32_t cnt = hi > lo ? hi - lo : 1;
    // AND, not OR: a pairing that consumes an index falls back the moment ONE
    // side lacks it, so the tile may only be costed as indexed when every row
    // in it is. Optimism here reintroduces the mis-pricing the flags exist to
    // remove.
    m.has_rank = m.has_occ = (hi > lo);
    for (uint32_t k = lo; k < hi; ++k) {
        const RowMeta& r = rows[order[k]].meta;
        m.has_rank = m.has_rank && r.has_rank;
        m.has_occ  = m.has_occ  && r.has_occ;
        card += r.cardinality;
        runs += r.n_runs;
        nnz  += r.n_nonzero_w;
        m.n_words = std::max(m.n_words, r.n_words);   // the universe: same for all
        if (r.cardinality) {
            m.first_set = std::min(m.first_set, r.first_set);
            m.last_set  = std::max(m.last_set,  r.last_set);
        }
    }
    m.cardinality = (uint32_t)(card / cnt);
    m.n_runs      = (uint32_t)(runs / cnt);
    m.n_nonzero_w = (uint32_t)(nnz  / cnt);
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

/* Probe-and-commit: turn a PREDICTED decision into a MEASURED one.
 *
 * Tile hoisting made selection cost 0.19% of runtime, which passed Gate 1 but
 * revealed that the model's decisions are not always good -- on neoverse-sve2
 * the tile choice measured 0.66x, worse than doing no selection at all. A
 * cheaper decision is a coarser decision, and cost-model error is now the
 * binding problem rather than cost-model expense.
 *
 * At tile granularity there is a remedy per-pair selection could never afford:
 * actually TIME the candidates on a handful of the tile's pairs and commit the
 * winner for the remaining thousands. A 64x64 tile is ~4,096 pairs; probing 3
 * pairs against 3 candidates is 9 kernel calls, ~0.2% of the tile, and it
 * replaces model error with measurement.
 *
 * Candidates are the model's top pick plus B x B, always. Including B x B
 * unconditionally is what makes this safe: the policy can never do worse than
 * all-bitmap by more than the probe cost, which bounds the downside that the
 * pure-model policy did not.
 */
Pairing probe_tile(const Kernels& K, const CostModel& model,
                   const std::vector<Row>& rows, const std::vector<uint32_t>& ord,
                   uint32_t i0, uint32_t i1, uint32_t j0, uint32_t j1,
                   const RowMeta& ta, const RowMeta& tb)
{
    const Pairing predicted = select_pairing(model, ta, tb);
    if (predicted == Pairing::BB || predicted == Pairing::Empty) return Pairing::BB;

    // Up to 3 representative pairs from the tile.
    uint32_t pi[3], pj[3];
    uint32_t np = 0;
    for (uint32_t k = 0; k < 3 && np < 3; ++k) {
        const uint32_t i = i0 + (uint32_t)((uint64_t)(i1 - i0) * k / 3);
        const uint32_t j = (j0 == i0) ? i + 1 + k : j0 + (uint32_t)((uint64_t)(j1 - j0) * k / 3);
        if (i < i1 && j < j1 && i != j) { pi[np] = i; pj[np] = j; ++np; }
    }
    if (np == 0) return predicted;

    const Pairing cand[2] = {predicted, Pairing::BB};
    double best_t = 1e300;
    Pairing best = Pairing::BB;
    for (Pairing p : cand) {
        const uint64_t t0 = now_ns();
        volatile uint64_t sink = 0;
        for (uint32_t k = 0; k < np; ++k) {
            const Row& a = rows[ord[pi[k]]];
            const Row& b = rows[ord[pj[k]]];
            const bool ad = a.meta.cardinality >= b.meta.cardinality;
            sink += run(K, p, ad ? a : b, ad ? b : a);
        }
        const double t = (double)(now_ns() - t0);
        (void)sink;
        if (t < best_t) { best_t = t; best = p; }
    }
    return best;
}

} // namespace

AllPairsStats allpairs_sum(const std::vector<Row>& rows, const CostModel& model,
                           Policy policy, uint32_t tile, bool no_zonemap, Pairing fixed_cell)
{
    AllPairsStats st;
    if (rows.size() < 2) return st;
    const Kernels K;
    const uint32_t n = (uint32_t)rows.size();

    // The sort is part of the cost and is timed with everything else
    // (RESEARCH_PLAN.md 5.2: "costs to account for honestly: the sort itself").
    const uint64_t t0 = now_ns();
    const std::vector<uint32_t> ord =
        (policy == Policy::PerTile || policy == Policy::Probe) ? density_order(rows) : [&]{
            std::vector<uint32_t> o(n); std::iota(o.begin(), o.end(), 0u); return o; }();

    double sel_ns = 0;
    uint64_t sum = 0, pairs = 0, decisions = 0, skipped = 0;

    for (uint32_t i0 = 0; i0 < n; i0 += tile) {
        const uint32_t i1 = std::min(i0 + tile, n);
        for (uint32_t j0 = i0; j0 < n; j0 += tile) {
            const uint32_t j1 = std::min(j0 + tile, n);

            Pairing tp = Pairing::BB;
            if (policy == Policy::PerTile || policy == Policy::Probe) {
                const uint64_t s0 = now_ns();
                const RowMeta a = tile_meta(rows, ord, i0, i1);
                const RowMeta b = tile_meta(rows, ord, j0, j1);
                tp = (policy == Policy::Probe)
                   ? probe_tile(K, model, rows, ord, i0, i1, j0, j1, a, b)
                   : select_pairing(model, a, b);
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

                    /* O(1) disjointness proof, kept PER PAIR even when the cell
                     * choice is hoisted to the tile.
                     *
                     * select_pairing() settles a pair with a.last < b.first (or
                     * the mirror) before it costs anything, and under a skewed
                     * spectrum that is most of the matrix. Hoisting the decision
                     * to the tile threw the test away with it, because a tile is
                     * only disjoint when EVERY pair in it is and that is almost
                     * never true. On census1881 the per-pair policy settled 63.3%
                     * of pairs here and the tile policy settled 0.0% -- it was
                     * running a kernel on two thirds of the matrix to compute
                     * zero, which is why it measured 15x worse than its own best
                     * fixed cell.
                     *
                     * The decision is what is expensive to make per pair; this
                     * test is two integer comparisons on metadata already in
                     * registers, and it is not a heuristic -- a pair failing it
                     * has an empty intersection by construction. Excluded from
                     * AllBitmap, which must stay the unassisted reference. */
                    if (policy != Policy::AllBitmap &&
                        (d.meta.cardinality == 0 || s.meta.cardinality == 0 ||
                         d.meta.last_set < s.meta.first_set ||
                         s.meta.last_set < d.meta.first_set)) {
                        ++st.cell_pairs[(int)Pairing::Empty];
                        ++skipped;
                        continue;
                    }

                    Pairing p = tp;
                    if (policy == Policy::Fixed) {
                        p = fixed_cell;
                    } else if (policy == Policy::AllBitmap) {
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
                    ++st.cell_pairs[(int)p];
                    if (p == Pairing::Empty) { ++skipped; continue; }
                    sum += run(K, p, d, s, no_zonemap || policy == Policy::AllBitmap);
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
