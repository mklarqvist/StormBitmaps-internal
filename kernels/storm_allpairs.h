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
 * It does not. Swept 8/16/32/64/128 on 16 corpora (standing instruction:
 * generic, not tuned to the dataset that raised the question), medians of
 * three whole-process runs:
 *
 *              T=8    T=16   T=32   T=64
 *   dim_008   1.03x  1.06x  1.13x  1.17x
 *   census1881 0.86x 0.80x  0.80x  0.84x
 *   as-skitter 0.91x 1.02x  1.07x  1.20x
 *   wiki-Talk  1.90x 1.90x  2.05x  1.97x
 *
 * Wider is better on the marginal corpora, not worse -- a wide tile amortises
 * the row loads across more pairs, and that locality outweighs the decision
 * error it introduces. So census1881's oracle gap is not a granularity
 * problem after all.
 *
 * Recorded because it was briefly changed to 32 on a SINGLE-SHOT sweep that
 * showed the opposite ordering. On this machine, run-to-run spread between
 * whole-process invocations reaches 30% at these working-set sizes; one run
 * per cell cannot rank widths that differ by 10%.
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
