/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist. Apache-2.0.
 */
#include "kernels/storm_allpairs.h"
#include "kernels/storm_cells.h"

#include <algorithm>
#include <cstdlib>
#include <numeric>
#include <time.h>

namespace storm {

bool br_orient_by_runs();   // defined in storm_cost.cpp
double br_orient_margin();  //   "

const char* name_of(Policy p) {
    switch (p) {
        case Policy::AllBitmap: return "all-bitmap";
        case Policy::PerPair:   return "per-pair";
        case Policy::PerTile:   return "per-tile";
        case Policy::Oracle:    return "oracle";
        case Policy::Fixed:     return "fixed-cell";
        case Policy::Probe:     return "probe";
        case Policy::Refine:    return "refine";
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

/* Variant override, for the per-corpus variant question.
 *
 * Kernels below hardcodes one variant per cell for every corpus. That
 * contradicts this project's own F10 finding, quoted in storm_cost.h: round 10
 * sampled the hardcoded constants and "the sampled optimum DIFFERED BY CORPUS
 * every time". The cost model selects the CELL per tile and then runs a
 * globally fixed VARIANT of it, so half the decision is still hardcoded.
 *
 * STORM_VARIANT_BS and friends let that half be swept before deciding whether
 * it deserves a selection mechanism of its own.
 *
 * SWEPT FOR B x S -- the cell that now carries 34-99% of pairs on nearly every
 * corpus, so the one with the most leverage. Per-tile vs fixed Roaring, medians
 * of three:
 *
 *                        ilp8  ilp8x  adapt  collapse  prefetch16  neon_idx
 *   census1881          0.94x  0.90x  0.87x    0.43x      0.54x      0.62x
 *   enwiki-categorylinks 3.67x 2.87x  2.74x    2.86x      2.90x      3.15x
 *   com-Orkut           2.09x  2.23x  2.13x    1.29x      1.61x      1.75x
 *
 * ilp8 is best or within noise of best everywhere, and the alternatives are not
 * marginally worse but badly worse -- collapse costs census1881 more than half
 * its throughput. So F10's "the optimum differs by corpus" does NOT hold for
 * this cell's variants at this point in the project: the hardcoded choice is
 * right, and per-corpus variant selection would be machinery in search of a
 * gain. Recorded so the question is not reopened without new variants.
 *
 * Note the shape of the result: `adaptive` -- which switches on whether the
 * bitmap is L1-resident, exactly the reasoning that failed three times in the
 * cost model this session -- is beaten by the unconditional ilp8 on all three. */
/* Cardinality at or below which B x S takes the peeled path. Default 4, the
 * bound cell_bs's `small` variant was written around; STORM_POINT_MAX sweeps
 * it, since the kernel's peel bound and the dispatch's best threshold are not
 * the same question -- the kernel pays the test per element and the dispatch
 * pays it once per pair.
 *
 * SWEPT, including pm=0 which disables the dispatch entirely. Medians of three:
 *
 *                        pm=0   pm=4   pm=8  pm=16  pm=32
 *   enwiki-categorylinks 2.93x  2.53x  3.31x  2.38x  4.60x
 *   gnomad_chr21         3.01x  2.98x  3.05x  2.94x  2.78x
 *   as-skitter           1.22x  1.24x  1.25x  1.31x  1.41x
 *   wiki-Talk            2.53x  2.72x  2.55x  2.57x  2.68x
 *   census1881_srt       3.79x  3.72x  3.49x  3.57x  1.93x
 *   com-Orkut            2.01x  2.11x  2.19x  2.06x  2.02x
 *
 * No threshold dominates: pm=32 is best on as-skitter (1.41x) and ruinous on
 * census1881_srt (3.79x -> 1.93x). Means across the six are 2.58 / 2.55 / 2.64
 * / 2.47 / 2.57 -- all inside the run-to-run spread. 4 stays.
 *
 * The dispatch's own worth is 6-12%, established by PAIRED A/B
 * (bench/ab.sh), which is the only method here that survives drift:
 *
 *   wiki-Talk       paired median off/on 1.118, on faster in 5/7 rounds
 *   as-skitter                           1.077,                  5/7
 *   gnomad_chr21                         1.061,                  5/7
 *   census1881_srt                       1.005,                  4/7
 *
 * Two earlier readings of this same change were both wrong, in opposite
 * directions, and both because of how they were measured. Comparing two whole
 * sweeps run at different times credited it with 20-35% (wiki-Talk 2.29x ->
 * 2.77x, census1881_srt 2.75x -> 3.73x). Comparing pm=0 against pm=4 as
 * unpaired medians within one sweep credited it with nothing. Alternating the
 * two configurations in time, so each measurement of one is adjacent to a
 * measurement of the other, gives 6-12% and a consistent sign. */
/* max/mean cardinality a tile must reach before its pairs are refined.
 *
 * 8, swept. Paired within one process (per-tile and refine share the run and
 * the warm-up, so the comparison carries no drift), median of 5, >1 means
 * refinement wins:
 *
 *                    g=1.5   g=3    g=8
 *   census1881       1.257  1.361  1.411
 *   dimension_008    0.621  0.947  1.007
 *   wiki-Talk        0.724  0.911  0.875
 *   dimension_003      -      -    0.976
 *   uscensus2000       -      -    0.991
 *   gnomad_chr21       -      -    0.984
 *
 * At 8 the gate separates: census1881 gains 41% and the corpora that do not
 * want refinement sit at parity, because the gate declines for them. Only
 * wiki-Talk still loses, by 12%, and it has margin (2.02x vs Roaring).
 *
 * This is the third predicate tried for this decision. The top-two cost ratio
 * gates on how close the CANDIDATES are and helps 2 of 14. The empty fraction
 * gates on how many pairs the span test settles, works per corpus, and fires
 * on locally-sparse tiles of corpora that do not want it. Heterogeneity gates
 * on the thing the mechanism is actually about -- how badly the tile aggregate
 * represents the rows it stands in for -- and is the first to separate all
 * three test corpora at one setting. */
static double het_gate() {
    static const double v = [] {
        if (const char* e = std::getenv("STORM_HET_GATE")) return std::atof(e);
        return 8.0;
    }();
    return v;
}

static uint32_t point_max() {
    static const uint32_t v = [] {
        if (const char* e = std::getenv("STORM_POINT_MAX")) {
            const long d = std::atol(e);
            if (d >= 0) return (uint32_t)d;
        }
        return 4u;
    }();
    return v;
}

static const char* variant_override(const char* env, const char* dflt) {
    const char* v = std::getenv(env);
    return (v && *v) ? v : dflt;
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
    fn_bb bb; fn_bb bb_plain; fn_bs bs; fn_bs bs_small; fn_br br; fn_bw bw;
    fn_ss ss; fn_sr sr; fn_rr rr; fn_ww ww;
    Kernels()
        : bb(pick(cell_bb(), chosen_variant("bb", "occ_sel"))), bb_plain(pick(cell_bb(), "dense")),
          bs(pick(cell_bs(), chosen_variant("bs", "ilp8"))),
          bs_small(pick(cell_bs(), "small")),
          br(pick(cell_br(), chosen_variant("br", "hybrid4"))), bw(pick(cell_bw(), chosen_variant("bw", "skip"))),
          ss(pick(cell_ss(), chosen_variant("ss", "adaptive2"))), sr(pick(cell_sr(), chosen_variant("sr", "adaptive2"))),
          rr(pick(cell_rr(), chosen_variant("rr", "adaptive2"))), ww(pick(cell_ww(), chosen_variant("ww", "skip2"))) {}
};

inline uint64_t run(const Kernels& K, Pairing p, const Row& d, const Row& s,
                    bool plain_bb = false) {
    switch (p) {
        case Pairing::BB: return plain_bb ? K.bb_plain(d.B(), s.B())
                                          : K.bb(d.B(), s.B());
        /* POINT DISPATCH: |S| <= 4 gets the peeled kernel.
         *
         * cell_bs has a `small` variant that peels |S| <= 4 -- "89% of real
         * 1KGP3 pairs" -- and it was unreachable, because Kernels binds one
         * B x S variant for the whole run. Selected as the global variant it
         * helps the small-cardinality corpora and costs the large ones
         * (enwiki-categorylinks 2.65x -> 3.02x, uscensus2000 2.29x -> 2.70x,
         * gnomad 2.80x -> 3.01x, against msprime_100k 1.73x -> 1.65x), because
         * its peel test sits inside the hot loop and every long list pays it.
         *
         * Hoisting the test here costs nothing: the dispatch already branches
         * on cardinality to orient the pair, so `s` is in a register and this
         * is one more compare on a branch that predicts almost perfectly (a
         * density-sorted tile is nearly all-small or nearly all-large). This is
         * the point representation for very low cardinality: at |S| <= 4 the
         * set IS its indices and no list machinery is worth entering. */
        case Pairing::BS: return s.meta.cardinality <= point_max() ? K.bs_small(d.B(), s.S())
                                                                   : K.bs(d.B(), s.S());
        /* Orient B x R by RUN COUNT, not by cardinality.
         *
         * The driver orients every pair by cardinality and hands B x R the
         * sparser side's runs. For B x S that is right -- the sparser side has
         * fewer elements by definition. For B x R it is not, because run count
         * and cardinality are not co-monotone: a DENSE row of a few long runs
         * has fewer runs than a sparse row of isolated bits, and B x R's cost
         * is the run count of whichever side supplies the runs.
         *
         * So the kernel can be handed the cheaper side. |A n B| is symmetric;
         * nothing else in the pair cares which row provides the bitmap.
         *
         * work_units(BR) returns min(ra, rb) to match -- which is what it
         * returned before it was "fixed" to track the driver's cardinality
         * orientation. That fix made the model agree with the code; this makes
         * the code agree with the algorithm, and the model is now right about
         * both. */
        /* Orient B x R by RUN COUNT rather than cardinality -- OFF by default
         * (STORM_BR_ORIENT=1 enables). See br_orient_by_runs() in storm_cost.cpp
         * for the measurements and why it is not the default. */
        case Pairing::BR: return (br_orient_margin() > 0.0 &&
                                  d.meta.n_runs * br_orient_margin() <= s.meta.n_runs)
                               ? K.br(s.B(), d.R())
                               : K.br(d.B(), s.R());
        // Same argument: the EWAH stream length, not the cardinality, is what
        // B x W pays for, and the shorter stream may belong to either side.
        case Pairing::BW: return (br_orient_margin() > 0.0 &&
                                  d.meta.n_runs * br_orient_margin() <= s.meta.n_runs)
                               ? K.bw(s.B(), d.W())
                               : K.bw(d.B(), s.W());
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

/* The tile's two best candidates by predicted cost.
 *
 * Used two ways: probe_tile() times both and commits the winner, and
 * Policy::Refine keeps both and lets each pair pick between them. Uncalibrated
 * cells are excluded, the same rule select_pairing() applies. */
struct Top2 { Pairing first = Pairing::BB, second = Pairing::BB; double ratio = 1e300; };

Top2 tile_top2(const CostModel& model, const RowMeta& ta, const RowMeta& tb) {
    Top2 t;
    double best1 = 0.0;
    t.first = select_pairing(model, ta, tb, &best1);
    if (t.first == Pairing::Empty) { t.second = Pairing::Empty; return t; }
    double best2 = 1e300;
    t.second = t.first;
    for (int i = 0; i < (int)Pairing::Empty; ++i) {
        const Pairing p = (Pairing)i;
        if (p == t.first || model.ns_per_unit[i] <= 0.0) continue;
        const double c = predict(model, p, ta, tb);
        if (c < best2) { best2 = c; t.second = p; }
    }
    t.ratio = best1 > 0.0 ? best2 / best1 : 1e300;
    return t;
}

/* When is per-pair refinement worth its own cost?
 *
 * Refine evaluates two candidates per pair, which is ~2 ns. That is free
 * against census1881's ~50 ns/pair and ruinous against dimension_008's ~6:
 * measured, refine costs dimension_008 6.1 -> 9.5 ns/pair and turns a 1.19x
 * win into 0.75x.
 *
 * Worse, on dimension_008 the per-pair decision is not merely expensive, it is
 * WORSE: the free-decision Oracle measures 7.4-9.6 against the tile policy's
 * 6.1. A tile aggregate is a smoothed estimate, and when the rows in a tile are
 * genuinely alike the smoothing removes noise the per-pair metadata still
 * carries. So refining is not a strictly-better-but-costlier option; it is a
 * trade, and it has to be gated on something.
 *
 * The gate is the tile's own top-two RATIO. If the runner-up is predicted many
 * times more expensive than the winner, no pair in that tile will flip -- the
 * within-tile spread of metadata cannot span that gap -- and evaluating both
 * per pair buys nothing. Refine only where the two candidates are close enough
 * that the choice is genuinely in doubt. O(1) per tile, zero per pair when it
 * declines.
 *
 * --- SWEPT, AND THE ANSWER IS "DO NOT REFINE" -------------------------------
 *
 * bench/refine_gate.sh, 14 corpora, medians of three. k=1 never refines and so
 * collapses exactly to PerTile; larger k refines more:
 *
 *                    k=1    k=2    k=3    k=5   k=inf
 *   census1881      0.88x  0.95x  1.09x  1.08x  0.96x   <- refine WINS
 *   weather_sept_85 1.05x  1.27x  1.34x  1.25x  1.30x   <- refine WINS
 *   dimension_008   1.08x  0.73x  0.70x  0.63x  0.66x
 *   dimension_033   3.15x  2.82x  1.81x  2.47x  3.01x
 *   soc-Pokec       2.29x  2.28x  1.90x  2.00x  1.93x
 *   com-LiveJournal 1.51x  1.35x  1.24x  1.20x  1.22x
 *   wiki-Talk       1.98x  1.68x  1.67x  1.47x  1.77x
 *   census1881_srt  2.86x  2.63x  2.13x  2.08x  2.48x
 *   wikileaks       5.14x  4.12x  4.84x  4.52x  3.30x
 *   as-skitter      1.01x  0.95x  0.94x  0.91x  0.91x
 *   gnomad_chr21    2.49x  2.31x  2.24x  2.13x  2.20x
 *   usher_sarscov2  1.72x  1.77x  1.62x  1.88x  1.34x
 *   census-income   1.72x  1.75x  1.71x  1.73x  1.72x
 *   dimension_003   1.80x  1.75x  1.80x  1.78x  1.73x
 *
 * k=1 is best or tied on 10 of 14. Refinement buys two corpora and costs most
 * of the rest, so it is OFF by default. Nor does a better gate rescue it: the
 * two it wins are the two with the highest per-pair cost (~50 and ~1300 ns,
 * where 2 ns of decision is free), but gating on predicted per-pair cost also
 * catches wikileaks (~85 ns, 5.14x -> 4.84x) and usher (~560 ns, 1.72x ->
 * 1.62x), where it loses. The benefit does not track any O(1) signal available
 * at tile time.
 *
 * Kept, gated, off. It is the right mechanism for a corpus whose tiles are
 * genuinely heterogeneous and whose pairs are expensive, and census1881 is
 * that corpus -- but "helps 2 of 14" is not a default. */
double refine_ratio() {
    static const double v = [] {
        if (const char* e = std::getenv("STORM_REFINE_RATIO")) {
            const double d = std::atof(e);
            if (d > 0.0) return d;
        }
        return 3.0;   // paired with het_gate(); see the heterogeneity gate above
    }();
    return v;
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
 * Candidates are the model's TOP TWO by predicted cost.
 *
 * They were "the model's top pick plus B x B, always", on the argument that an
 * unconditional B x B bounds the downside: the policy could never do worse than
 * all-bitmap by more than the probe cost. That bound is vacuous, because the
 * probe cost IS all-bitmap. Timing three B x B pairs at universe 1.3e8 is three
 * passes over 32 MB, ~500 microseconds each, per tile -- and on
 * enwiki-categorylinks it took the probe policy to 1481 ns/pair against the
 * tile policy's 12.29, a 120x loss produced entirely by measuring a candidate
 * the model had already ranked four orders of magnitude behind.
 *
 * Top-two keeps what probing is for. Probing exists to correct model error, and
 * model error is a factor, not an ordering catastrophe: when the model is close
 * enough to be wrong it is close enough to have ranked the true winner second.
 * When it puts B x B second, B x B still gets probed -- on the dense corpora
 * where that matters (weather_sept_85, census-income) it is second on merit.
 * What no longer happens is paying for it where the model is certain.
 */
Pairing probe_tile(const Kernels& K, const CostModel& model,
                   const std::vector<Row>& rows, const std::vector<uint32_t>& ord,
                   uint32_t i0, uint32_t i1, uint32_t j0, uint32_t j1,
                   const RowMeta& ta, const RowMeta& tb)
{
    const Pairing predicted = tile_top2(model, ta, tb).first;
    if (predicted == Pairing::Empty) return Pairing::BB;
    const Pairing second = tile_top2(model, ta, tb).second;
    if (second == predicted) return predicted;

    // Up to 3 representative pairs from the tile.
    uint32_t pi[3], pj[3];
    uint32_t np = 0;
    for (uint32_t k = 0; k < 3 && np < 3; ++k) {
        const uint32_t i = i0 + (uint32_t)((uint64_t)(i1 - i0) * k / 3);
        const uint32_t j = (j0 == i0) ? i + 1 + k : j0 + (uint32_t)((uint64_t)(j1 - j0) * k / 3);
        if (i < i1 && j < j1 && i != j) { pi[np] = i; pj[np] = j; ++np; }
    }
    if (np == 0) return predicted;

    const Pairing cand[2] = {predicted, second};
    double best_t = 1e300;
    Pairing best = predicted;
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

    /* Oracle's decisions are made BEFORE the clock starts.
     *
     * The policy is documented as "the model's choice with a zero-cost
     * decision, the upper bound a perfect free selector could reach", but it
     * called select_pairing() inside the timed loop and merely declined to
     * attribute the time to ns_selection. So it reported the cost of per-pair
     * selection as though it were kernel time: on dimension_008, 49.10 ns/pair
     * against the tile policy's 6.60 for byte-identical decisions, ~42 ns of
     * which was the hidden decision. A regret denominator larger than the
     * policy it bounds is not a bound.
     *
     * Precomputing costs O(N^2) bytes, which is why this stays a diagnostic
     * policy and not a shipping one -- the whole point of M3 is that a real
     * system cannot afford the per-pair decision, in time OR in space. */
    std::vector<uint8_t> oracle_plan;
    if (policy == Policy::Oracle) {
        oracle_plan.assign((size_t)n * n, (uint8_t)Pairing::BB);
        for (uint32_t i = 0; i < n; ++i)
            for (uint32_t j = i + 1; j < n; ++j) {
                const bool id = rows[i].meta.cardinality >= rows[j].meta.cardinality;
                oracle_plan[(size_t)i * n + j] = (uint8_t)select_pairing(
                    model, rows[id ? i : j].meta, rows[id ? j : i].meta);
            }
    }

    // The sort is part of the cost and is timed with everything else
    // (RESEARCH_PLAN.md 5.2: "costs to account for honestly: the sort itself").
    /* Oracle traverses in density order too.
     *
     * It did not, and that made it useless as the regret denominator: on
     * dimension_008 the oracle and the tile policy reached byte-identical
     * decisions -- same cell for 100% of pairs -- and measured 40.72 against
     * 8.10 ns/pair. The whole 5x was row ORDER. An "unattainable upper bound"
     * that loses 5x to the policy it bounds is measuring the sort, not the
     * selection, and would have reported negative regret everywhere.
     *
     * Worth stating on its own: with the decisions held fixed, density-sorted
     * traversal is worth 5x on dimension_008. Sec 5.2 justified the sort as what
     * makes tile hoisting sound -- homogeneous tiles, so one decision fits every
     * pair in it. This says it also pays for itself in locality, independently
     * of whether anything is being hoisted.
     *
     * PerPair keeps identity order deliberately: it is the "decide everything
     * from scratch, arrange nothing" reference. */
    const uint64_t t0 = now_ns();
    const std::vector<uint32_t> ord =
        (policy == Policy::PerTile || policy == Policy::Probe ||
         policy == Policy::Oracle || policy == Policy::Refine) ? density_order(rows) : [&]{
            std::vector<uint32_t> o(n); std::iota(o.begin(), o.end(), 0u); return o; }();

    double sel_ns = 0;
    uint64_t sum = 0, pairs = 0, decisions = 0, skipped = 0;

    for (uint32_t i0 = 0; i0 < n; i0 += tile) {
        const uint32_t i1 = std::min(i0 + tile, n);
        for (uint32_t j0 = i0; j0 < n; j0 += tile) {
            const uint32_t j1 = std::min(j0 + tile, n);

            Pairing tp = Pairing::BB, tp2 = Pairing::BB;
            if (policy == Policy::PerTile || policy == Policy::Probe ||
                policy == Policy::Refine) {
                const uint64_t s0 = now_ns();
                const RowMeta a = tile_meta(rows, ord, i0, i1);
                const RowMeta b = tile_meta(rows, ord, j0, j1);
                if (policy == Policy::Probe) {
                    tp = probe_tile(K, model, rows, ord, i0, i1, j0, j1, a, b);
                } else if (policy == Policy::Refine) {
                    const Top2 t = tile_top2(model, a, b);
                    tp = t.first;
                    /* Gate refinement on tile HETEROGENEITY.
                     *
                     * Refining is worth its per-pair cost exactly when the tile
                     * aggregate is a poor stand-in for the rows it summarises.
                     * The empty fraction was tried as a proxy and gates
                     * correctly per corpus but not globally -- it fires on
                     * locally-sparse tiles of corpora that do not want it.
                     * max/mean cardinality measures the departure directly,
                     * and rows are density-sorted so within a tile it is a
                     * clean spread statistic. One pass over <=64 metadata
                     * records per tile decision. */
                    auto spread = [&](uint32_t lo, uint32_t hi) {
                        uint64_t sum = 0; uint32_t mx = 0;
                        for (uint32_t k = lo; k < hi; ++k) {
                            const uint32_t c = rows[ord[k]].meta.cardinality;
                            sum += c; if (c > mx) mx = c;
                        }
                        const double mean = hi > lo ? (double)sum / (hi - lo) : 0.0;
                        return mean > 0.0 ? (double)mx / mean : 1.0;
                    };
                    const double het = std::max(spread(i0, i1), spread(j0, j1));
                    const bool heterogeneous = het >= het_gate();
                    // Decline to refine when the runner-up is far behind: no
                    // pair in this tile can flip, so the per-pair evaluation
                    // would be pure overhead. Collapses to PerTile exactly.
                    tp2 = (heterogeneous && t.ratio <= refine_ratio()) ? t.second : t.first;
                } else {
                    tp = select_pairing(model, a, b);
                }
                if (tp == Pairing::Empty) tp = Pairing::BB;   // never skip a whole tile
                if (tp2 == Pairing::Empty) tp2 = tp;
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
                    if (policy == Policy::Refine) {
                        // Two predict() calls on metadata already in registers.
                        // Not timed into sel_ns separately: it is per-pair work
                        // and belongs in the total, which is the honest place
                        // for it -- the claim is that the total goes DOWN.
                        if (tp2 != tp)
                            p = predict(model, tp2, d.meta, s.meta) <
                                predict(model, tp,  d.meta, s.meta) ? tp2 : tp;
                    } else if (policy == Policy::Fixed) {
                        p = fixed_cell;
                    } else if (policy == Policy::AllBitmap) {
                        p = Pairing::BB;
                    } else if (policy == Policy::PerPair) {
                        const uint64_t s0 = now_ns();
                        p = select_pairing(model, d.meta, s.meta);
                        sel_ns += (double)(now_ns() - s0);
                        ++decisions;
                    } else if (policy == Policy::Oracle) {
                        // Table lookup only -- the decision was made above, off
                        // the clock. Still not a true oracle: it is the model's
                        // choice, so it bounds free PERFECT-MODEL selection, not
                        // free perfect selection. A true oracle would have to
                        // time every cell on every pair.
                        const uint32_t ri = ord[i], rj = ord[j];
                        p = (Pairing)oracle_plan[ri < rj ? (size_t)ri * n + rj
                                                         : (size_t)rj * n + ri];
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
