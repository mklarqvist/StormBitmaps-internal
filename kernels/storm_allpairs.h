/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist. Apache-2.0.
 *
 * The batched all-pairs driver — RESEARCH_PLAN.md §6 (the deliverable), §5.2
 * (density-sorted tiling) and M3 (tile-hoisted selection).
 *
 * Three open items collapse into this one component, which is why it is worth
 * building before anything else remaining:
 *
 *  1. §6 API. Every kernel in this project has the signature
 *     `(view, view) -> count`. There has never been a batched entry point, and
 *     §6 calls the batched primitive "the deliverable".
 *
 *  2. M3 / Gate 1. Per-pair selection measured 28.3% of runtime against a 2%
 *     budget (OPTLOG, M4 section). §8 Phase 1 prescribes tile hoisting as the
 *     remedy: decide once per TILE of pairs rather than once per pair. With a
 *     tile of TR x TC rows that is one decision amortized over TR*TC pairs.
 *
 *  3. F11 / cross-pair reuse. At DRAM residency the dense cell is bandwidth
 *     bound and SIMD stops helping, but a row loaded once and paired against a
 *     whole column panel is read once instead of TC times. No pairwise kernel
 *     can express that.
 *
 * Density-sorted order (§5.2) is what makes tile hoisting sound: sorting rows
 * by cardinality means the rows in a tile have similar shape, so one decision
 * for the tile is close to the decision each pair would have made. Without the
 * sort, tiles are heterogeneous and hoisting trades accuracy for speed. The
 * permutation is tracked so results are reported in original row order.
 */
#ifndef STORM_ALLPAIRS_H_
#define STORM_ALLPAIRS_H_

#include <cstdint>
#include <cstddef>
#include <vector>

#include "kernels/storm_repr.h"
#include "kernels/storm_cost.h"

namespace storm {

enum class Policy : uint8_t {
    AllBitmap,    // always B x B -- the P1 reference, what everyone else does
    PerPair,      // M2/M4 per-pair selection: correct, and measured too slow
    PerTile,      // M3: one decision per tile, from tile-aggregate metadata
    Oracle,       // best cell per pair, unattainable; the regret denominator
    Fixed,        // DIAGNOSTIC: one cell for every pair, no selection at all.
                  // Isolates the harness's own per-pair dispatch cost from the
                  // selector's decision quality -- if Fixed does not reproduce
                  // the tight-loop figure bench_real measures for the same cell,
                  // the gap is dispatch, not selection.
    Probe,        // M3 + measured commit: time the candidates on a few pairs of
                  // the tile, then commit for the rest. See storm_allpairs.cpp.
    /* Hoist the decision BOUNDARY, not the decision.
     *
     * PerTile commits one cell for a whole tile, and on a heterogeneous corpus
     * one cell cannot be right for every pair in it. census1881 is the clean
     * case: a free per-pair selector (Oracle) measures 46.70 ns/pair against
     * fixed Roaring's 51.91 and wins, while PerTile measures 54.47 and loses --
     * the winning decisions exist and tile granularity discards them. The mix
     * says exactly what is lost: Oracle routes 9.1% of pairs to B x R, PerTile
     * folds all of them into B x S.
     *
     * PerPair is not the answer either; deciding from scratch costs ~25 ns/pair
     * against a 2 ns budget, because select_pairing() prices ten candidates.
     *
     * But almost all of that cost is spent discarding candidates that were
     * never plausible for this tile. So: rank the candidates ONCE per tile,
     * keep the top two, and let each pair choose between just those two -- two
     * predict() calls on metadata already in registers. The expensive part of
     * selection is hoisted and the part that actually varies per pair is not.
     *
     * --- MEASURED, AND IT IS DECISIVE IN BOTH DIRECTIONS -------------------
     *
     * bench_allpairs prints per-tile and refine from the SAME process and the
     * same warm-up, so comparing those two lines within each run is perfectly
     * paired -- no drift, no ordering. Median of 7 runs, >1 means refine wins:
     *
     *                     k=2    k=3    k=8
     *   census1881       1.028  1.308  1.327     <- refine 31% FASTER
     *   dimension_008    0.565  0.575  0.559     <- per-tile 1.8x faster
     *   wiki-Talk        0.665  0.688  0.717     <- per-tile 1.4x faster
     *
     * Refine at k=3 would take census1881 -- the last corpus still losing to
     * fixed Roaring -- from 0.91x to roughly 1.20x, and cost dimension_008 44%.
     * Enabling it globally trades one loss for another; that is why
     * refine_ratio() defaults to 1 (never refine).
     *
     * THE CANDIDATE PREDICATE, which earlier attempts did not have: the empty
     * fraction. census1881 settles 63.3% of its pairs by the O(1) span test;
     * dimension_008 settles 12.6% and wiki-Talk 14.4%. That ordering matches
     * the result exactly, and it is mechanistically sensible -- when most pairs
     * are disjoint, the tile aggregate is computed over rows whose pairs will
     * never run a kernel, so it describes the live pairs worst precisely where
     * per-pair refinement is cheapest to amortise.
     *
     * It is not yet usable: the empty fraction is known after the tile is
     * walked, not when its decision is made. Estimating it at decision time
     * from the two tiles' [first_set, last_set] spans is the obvious next step
     * and is O(1), but it is unbuilt and unmeasured. */
    Refine,
};

const char* name_of(Policy p);

struct AllPairsStats {
    uint64_t sum          = 0;   // sum of |Xi n Xj| over all pairs
    uint64_t pairs        = 0;
    uint64_t decisions    = 0;   // how many selection decisions were actually made
    double   ns_total     = 0;
    double   ns_selection = 0;   // time inside the selection path only
    uint64_t skipped      = 0;   // pairs settled as provably disjoint, no kernel run
    /* Pairs routed to each cell, indexed by Pairing.
     *
     * An aggregate ns/pair says selection helped or did not; it cannot say
     * WHICH decision was wrong when it did not. On census1881 the tile policy
     * measured 15x worse than its own best fixed cell, and no amount of staring
     * at the total identified the tile responsible. This is the cheapest
     * possible instrument -- one increment on a path that already branches --
     * and it turns "the selector is bad here" into "the selector sent 41% of
     * pairs to B x B". */
    uint64_t cell_pairs[(int)Pairing::COUNT] = {0};
};

/* Form 1 of §6: reduction. No N^2 materialization, so this is the regime where
 * kernel and selection speed dominate and nothing is hidden behind output
 * bandwidth (§3.5). */
/* `no_zonemap` forces even the SELECTOR's B x B to the unfiltered kernel, so the
 * zone map can be ablated independently of representation selection. Without it
 * the two are confounded: a "selection" speedup partly reflects the filter. */
/* Default tile width 64, from bench/tile_size.sh.
 *
 * The width is the decision-quality / decision-cost knob, and the cost side is
 * not binding: selection measures 0.12% of runtime against a 2% gate. So the
 * question is purely whether a narrower tile makes better decisions, which
 * census1881 suggested it should -- its per-pair oracle sits at ~42 ns/pair
 * against a tile policy at ~52, decisions that exist and are being discarded
 * by granularity.
 *
 * It does not, and there is no corpus-level rule either. Swept twice, the
 * second time after six model fixes, medians of three whole-process runs:
 *
 *   corpus            row_kB   T=8   T=16   T=32   T=64  T=128
 *   msprime_10k            2  4.44x 4.68x  4.40x  3.93x  3.72x
 *   census-income         24    -   1.74x  1.75x  1.71x    -
 *   msprime_100k          24    -   1.74x  1.62x  1.81x    -
 *   weather_sept_85      123  1.18x 1.28x  1.21x  1.23x  1.07x
 *   wikileaks-noquotes   165    -   5.95x  5.44x  5.77x    -
 *   as-skitter           207  0.96x 1.05x  1.09x  1.16x  1.13x
 *   msprime_1M           244    -   1.82x  1.72x  1.47x    -
 *   wiki-Talk            292  2.03x 2.19x  2.21x  2.48x  2.56x
 *   dimension_008        472  1.02x 1.10x  1.25x  1.26x  1.22x
 *   dimension_033        472  3.21x 3.21x  3.34x  3.39x  3.33x
 *   census1881           522  0.91x 0.72x  0.92x  0.91x  0.74x
 *   census1881_srt       522    -   4.03x  4.21x  4.26x    -
 *
 * The obvious hypothesis -- that a tile should be sized so its working set
 * fits L2, which would mean SMALL tiles for the 522 kB rows and large ones for
 * the 2 kB rows -- is contradicted outright: census1881 at 522 kB/row is worst
 * at T=16, and msprime_10k at 2 kB/row is best there. The reverse hypothesis
 * fails too, because msprime_10k (2 kB) and msprime_1M (244 kB) both prefer 16
 * while msprime_100k (24 kB) and census1881_srt (522 kB) both prefer 64. Row
 * size does not order the optimum in either direction.
 *
 * Most of the spread is noise. census1881 reads 0.91x / 0.72x / 0.92x / 0.91x
 * across T = 8/16/32/64 -- three agreeing values and one outlier, which is what
 * a 26% run-to-run swing looks like, not a tile-width effect. The differences
 * that survive as plausibly real (msprime_10k and msprime_1M favouring 16 by
 * ~20%) are not shared by their own siblings, so no rule generalises from them.
 *
 * T=64 stands. Recorded in full because it was briefly changed to 32 on a
 * SINGLE-SHOT sweep that showed a different ordering, and because this is the
 * second sweep to reach the same answer -- a third is not worth its runtime.
 */
AllPairsStats allpairs_sum(const std::vector<Row>& rows,
                           const CostModel& model,
                           Policy policy,
                           uint32_t tile = 64,
                           bool no_zonemap = false,
                           Pairing fixed_cell = Pairing::BS);

/* Form 2 of §6: tile visitor. The caller consumes each tile of counts, so a
 * consumer can stream or fuse without ever holding N^2. This is the form
 * Tomahawk needs (RESEARCH_PLAN.md §8b). */
typedef void (*TileCallback)(uint32_t i0, uint32_t j0,
                             uint32_t nr, uint32_t nc,
                             const uint32_t* counts, void* ctx);

AllPairsStats allpairs_tiles(const std::vector<Row>& rows,
                             const CostModel& model,
                             Policy policy,
                             TileCallback cb, void* ctx,
                             uint32_t tile = 64);

} // namespace storm

#endif // STORM_ALLPAIRS_H_
