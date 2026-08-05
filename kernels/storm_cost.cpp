/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist
 * Licensed under the Apache License, Version 2.0.
 */
#include "kernels/storm_cost.h"
#include "kernels/storm_cells.h"
#include "kernels/storm_gen.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <time.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace storm {

const char* name_of(Pairing p) {
    switch (p) {
        case Pairing::BB: return "B x B";  case Pairing::BS: return "B x S";
        case Pairing::BR: return "B x R";  case Pairing::BW: return "B x W";
        case Pairing::SS: return "S x S";  case Pairing::SR: return "S x R";
        case Pairing::SW: return "S x W";  case Pairing::RR: return "R x R";
        case Pairing::RW: return "R x W";  case Pairing::WW: return "W x W";
        case Pairing::Empty: return "empty";
        default: return "?";
    }
}

/* Predicted work, in each cell's own unit.
 *
 * These are the asymptotics of RESEARCH_PLAN.md 1, written down. The point of
 * separating work from rate is portability: the work function is a property of
 * the algorithm and is the same everywhere, while ns_per_unit is a property of
 * the machine and must be measured on it. A model that folded them together
 * would need recalibrating for every corpus shape, not just every host.
 *
 * Orientation is handled here rather than in the kernels: the asymmetric cells
 * always iterate the SPARSER side, so the work is the min, not the argument
 * order. A caller that gets this backwards would be modelling a kernel nobody
 * runs.
 */
/* B x B is NO LONGER Theta(m).
 *
 * The model was written when bitmap x bitmap meant "touch every word", which is
 * the premise of PROBLEM_STATEMENT.md 2 and was true of every B x B kernel in
 * the project until the zone map landed. It is not true of occ_sel, which
 * visits only the bins live on both sides -- so predicting its cost from m
 * over-estimates it by exactly the factor the zone map saves, which on skewed
 * data is 5-25x.
 *
 * That mis-prediction is why M4 v1 chose against B x B on almost every pair and
 * measured a 151% regret. The fix is to charge B x B for the work it actually
 * does. The zone-map overlap is the exact bin count, but computing it costs
 * m/512, so the model uses the cheap metadata proxy instead: expected live bins
 * under independence, from the two occupancy fractions. Approximate, O(1), and
 * good enough to rank -- which is all a selector needs.
 */
static double bb_expected_bins(const RowMeta& a, const RowMeta& b) {
    const double bins = std::max(1.0, (double)std::max(a.n_words, b.n_words) / 8.0);
    const double fa = std::min(1.0, (double)a.n_nonzero_w / std::max(1u, a.n_words));
    const double fb = std::min(1.0, (double)b.n_nonzero_w / std::max(1u, b.n_words));
    // A bin is live if any of its 8 words is set: P ~ 1-(1-f)^8, linearized to
    // min(1, 8f). std::pow was tried here and cost more than std::log2 did --
    // the same mistake twice in one function. A selector that has to rank ten
    // candidates cannot afford a transcendental in any of them, and a linear
    // bound ranks identically over the range that matters.
    const double pa = std::min(1.0, 8.0 * fa);
    const double pb = std::min(1.0, 8.0 * fb);
    return std::max(1.0, bins * pa * pb * 8.0);   // in WORDS visited
}

/* Distinct words the sparser side occupies = scattered probes B x S will make.
 * B x R and B x W walk RANGES, so their probes are sequential within a run and
 * one miss serves the several that follow; they are NOT charged this. Pricing
 * all three alike inverted the ranking between them and made dimension_033
 * abandon B x R (72% of pairs, 3.05x) for a 0.46x mixture. */
/* Multiplier on the measured cold-probe excess. STORM_PROBE_SCALE overrides.
 *
 * 1.0 would use the microbenchmark's number as it stands, and that is 20x too
 * large. Measured excess is 1.125 ns/touch (probe_cold 2.162 - probe_hot 1.036
 * at a 16 MiB P-core L2); applying it in full costs census1881 0.91x -> 0.57x,
 * enwiki-categorylinks 2.76x -> 1.04x and msprime_1M 4.09x -> 2.11x.
 *
 * The microbenchmark measures the wrong access pattern. It walks a prime
 * stride, which is deliberately prefetch-hostile, whereas B x S probes at the
 * sparse side's positions -- which are SORTED and, on every corpus here,
 * clustered. Ascending clustered probes hit the same line repeatedly and let
 * the prefetcher run ahead; the two patterns differ by more than an order of
 * magnitude and only one of them is B x S.
 *
 * So 0.05 is fitted, tier 2, and labelled. It reproduces the coefficient that
 * measured 22/23 (0.25 element-units at ns_per_unit[BS] = 0.224 is 0.056
 * ns/touch). The honest fix is a microbenchmark that probes in ascending order
 * at a realistic gap distribution rather than a hostile stride -- at which
 * point the scale should go to 1.0 and this knob should disappear. Open item;
 * the measured rates stay in CostModel so it can be done without re-deriving
 * them. */
static double probe_scale() {
    static const double v = [] {
        if (const char* e = std::getenv("STORM_PROBE_SCALE")) return std::atof(e);
        return 0.05;
    }();
    return v;
}

static double bs_touches(const RowMeta& a, const RowMeta& b) {
    return (a.cardinality <= b.cardinality) ? (double)a.n_nonzero_w
                                            : (double)b.n_nonzero_w;
}

static double work_units(Pairing p, const RowMeta& a, const RowMeta& b) {
    const double m  = (double)std::max(a.n_words, b.n_words);
    const double sa = a.cardinality, sb = b.cardinality;
    const double ra = a.n_runs,      rb = b.n_runs;
    /* EWAH stream length = markers PLUS LITERALS.
     *
     * This was 2r+1, the marker count alone, with a comment conceding it
     * omitted "plus its literals". That is not a close bound, it is the wrong
     * term: a literal is emitted for every bitmap word that is neither all-zero
     * nor all-ones, so on scattered data the stream is almost entirely
     * literals and 2r+1 understates it by the ratio of words to runs.
     *
     * The consequence was a coin flip. work_units(BW) = 2r+1 against
     * work_units(BR) = r, with calibrated rates 0.426 and 0.797, put B x W and
     * B x R within 7% of each other on run-structured data -- so the selector
     * chose between them essentially at random across runs, and B x W is much
     * the worse of the two in fact. dimension_033 was seen at B x R=72% (3.33x)
     * and at B x W=72% (1.98x) on consecutive sweeps of identical code, and
     * usher_sarscov2 lost outright (1.71x -> 0.91x) when 14% of its pairs
     * flipped from B x R to B x W.
     *
     * n_nonzero_w is the literal count: words with at least one bit set. It
     * over-counts by the all-ones words, which are fills rather than literals,
     * but those are rare below density 0.5 and the error is in the safe
     * direction for a cell that is a labelled loser on 16 of 17 corpora. */
    const double wa = 2.0 * ra + 1.0 + (double)a.n_nonzero_w;
    const double wb = 2.0 * rb + 1.0 + (double)b.n_nonzero_w;

    /* Which side the asymmetric cells actually iterate.
     *
     * allpairs_sum orients every pair by CARDINALITY -- the denser row supplies
     * the bitmap, the sparser row supplies the list, runs or EWAH stream -- so
     * B x R costs the sparser row's RUN count, whatever that happens to be.
     *
     * This used min(ra, rb), which is a different quantity and not a bound on
     * it: run count and cardinality are not co-monotone. A dense row of a few
     * long runs has FEWER runs than a sparse row of isolated bits, so the min
     * returns the run count of the side the kernel will not iterate, and B x R
     * gets priced as though it were reading the wrong row. On dimension_008
     * that steered 58.9% of pairs to B x S when B x R applied to everything was
     * faster than the tile policy's whole mixture (7.34 vs 8.10 ns/pair).
     *
     * min() was correct for B x S only by accident: the sparser side is the one
     * with smaller cardinality by definition, so there min IS the sparse side. */
    const bool a_sparse = sa <= sb;
    const double s_card = a_sparse ? sa : sb;      // sparser side
    const double s_runs = a_sparse ? ra : rb;
    const double s_ewah = a_sparse ? wa : wb;
    const double d_runs = a_sparse ? rb : ra;      // denser side
    const double d_ewah = a_sparse ? wb : wa;

    /* The number of distinct 64-bit words the sparse side occupies -- how many
     * scattered bitmap probes B x S will make. Returned to predict() via
     * bs_touches() rather than folded in here: the COUNT is algorithmic, the
     * price of one touch is machine-dependent, and mixing them is what put a
     * fitted constant inside this function. */
    (void)0;
    switch (p) {
        case Pairing::BB: return bb_expected_bins(a, b);
        // The touch term is charged to B x S ALONE. B x S probes the dense
        // bitmap at scattered positions, one line per distinct word. B x R and
        // B x W walk RANGES, so their probes are sequential within a run and
        // one miss serves the several that follow -- charging them the same
        // per-touch price is what made dimension_033 abandon B x R (72% of
        // pairs, 3.05x) for a B x S / B x W / W x W mixture at 0.46x, and it
        // did so at every positive weight down to 0.125.
        case Pairing::BS: return s_card;
        case Pairing::BR: return s_runs;
        case Pairing::BW: return s_ewah;
        // Integer log2, NOT std::log2. The first version of this called the
        // libm double routine inside the selection loop and selection cost
        // measured 24 ns/pair -- 36% of runtime against a 2% gate. Standing
        // rule 7 says the decision must be near-free, and a transcendental in
        // the hot path is the opposite of that.
        case Pairing::SS: {
            const uint32_t mx = (uint32_t)std::max(2.0, std::max(sa, sb));
            return std::min(sa, sb) * (double)(32 - __builtin_clz(mx));
        }
        // Both merge kernels walk the sparse side's list against the dense
        // side's runs/stream -- again the orientation the driver imposes, not
        // the cheaper of the two orderings.
        case Pairing::SR: return s_card + d_runs;
        case Pairing::SW: return s_card + d_ewah;
        case Pairing::RR: return ra + rb;
        case Pairing::RW: return std::min(ra + wb, rb + wa);
        case Pairing::WW: return wa + wb;
        default:          return 0.0;
    }
}

double predict(const CostModel& m, Pairing p, const RowMeta& a, const RowMeta& b) {
    if (p == Pairing::Empty) return 0.0;
    /* B x B WITHOUT a zone map is a different algorithm and must be priced as
     * one. bb_occ_sel falls back to bb_dense the moment either side's occ is
     * null, and bb_dense is Theta(m) -- the original premise of
     * PROBLEM_STATEMENT.md 2, unfiltered. Since C33 gated zone-map construction
     * on n*512 > universe, most sparse rows no longer carry one, so charging
     * them the filtered cost under-prices the cell by the entire factor the
     * filter would have saved. Return before the occ term below, which is also
     * not paid when there is no occ to read. */
    if (p == Pairing::BB && !(a.has_occ && b.has_occ))
        return m.ns_fixed + m.ns_per_unit[(int)p] *
                            (double)std::max(a.n_words, b.n_words);
    double c = m.ns_fixed + m.ns_per_unit[(int)p] * work_units(p, a, b);
    if (p == Pairing::BS && m.ns_probe_cold > m.ns_probe_hot) {
        /* Only the EXCESS over a cache-resident probe, not the whole probe.
         *
         * ns_per_unit[BS] is calibrated on a 98 kB synthetic corpus, so the
         * cost of one L1-resident probe is ALREADY inside it -- adding the full
         * cold-probe cost on top would count it twice and over-price B x S by
         * the hot rate on every pair, including the cache-resident ones the
         * calibration describes correctly.
         *
         * Charged unconditionally rather than gated on one row's size, because
         * residency in an all-pairs sweep is a property of the CORPUS, not of a
         * row: census1881's bitmap is 535 kB against a 16 MiB L2 and would look
         * resident, but 373 of them cycle through that L2 every tile, so the
         * probe is cold in fact. The gated variant is measured below. */
        c += (m.ns_probe_cold - m.ns_probe_hot) * probe_scale() * bs_touches(a, b);
    }
    if (p == Pairing::BB) {
        /* The zone-mapped B x B must AND both occupancy maps before it can skip
         * anything: m/512 words, unavoidable and O(m).
         *
         * bb_expected_bins() charges only the bins actually VISITED, which on a
         * sparse row floors at 1 -- so B x B predicted 2.1 ns on uscensus2000
         * where it truly costs 215.8, and the selector chose it over B x S's
         * 23.5 on essentially every pair. That is the whole of the selector's
         * 7-16x loss on the large-universe sparse corpora.
         *
         * ns_per_occ_word was already declared and calibrated for exactly this
         * term and was simply never read by predict(). */
        const double occ_words =
            std::max(1.0, (double)std::max(a.n_words, b.n_words) / 512.0);
        c += m.ns_per_occ_word * occ_words;
    }
    return c;
}

Pairing select_pairing(const CostModel& m, const RowMeta& a, const RowMeta& b,
                       double* predicted)
{
    // Two O(1) proofs of emptiness before any cost is computed. Under a 1/i
    // spectrum a large fraction of pairs are settled here.
    if (a.cardinality == 0 || b.cardinality == 0 ||
        a.last_set < b.first_set || b.last_set < a.first_set) {
        if (predicted) *predicted = 0.0;
        return Pairing::Empty;
    }

    Pairing best = Pairing::BB;
    double  bestc = predict(m, Pairing::BB, a, b);
    for (int i = 0; i < (int)Pairing::Empty; ++i) {
        const Pairing p = (Pairing)i;
        if (m.ns_per_unit[i] <= 0.0) continue;          // uncalibrated: not a candidate
        const double c = predict(m, p, a, b);
        if (c < bestc) { bestc = c; best = p; }
    }
    if (predicted) *predicted = bestc;
    return best;
}

/* Measured on this host (Apple M4, results/iter10_raw.txt), in nanoseconds per
 * unit of each cell's own work. Present so the model is usable without paying
 * calibration at startup, and WRONG anywhere else -- which is precisely why
 * calibrate() exists. Anyone shipping this on another machine and trusting
 * these numbers is making the mistake this whole section is about. */
void default_model(CostModel& m) {
    m.ns_per_unit[(int)Pairing::BB] = 0.116;   // per word
    m.ns_per_unit[(int)Pairing::BS] = 0.30;    // per list element
    m.ns_per_unit[(int)Pairing::BR] = 0.60;    // per run
    m.ns_per_unit[(int)Pairing::BW] = 0.55;    // per ewah word
    m.ns_per_unit[(int)Pairing::SS] = 0.22;    // per elt*log
    m.ns_per_unit[(int)Pairing::SR] = 0.35;    // per elt+run
    m.ns_per_unit[(int)Pairing::SW] = 0.40;
    m.ns_per_unit[(int)Pairing::RR] = 0.30;
    m.ns_per_unit[(int)Pairing::RW] = 0.45;
    m.ns_per_unit[(int)Pairing::WW] = 0.50;
    m.ns_fixed        = 2.0;
    m.ns_per_occ_word = 0.30;
    m.calibrated      = false;   // NOT calibrated: these are one host's numbers
}

// ---------------------------------------------------------------------------
static inline uint64_t cost_now_ns() {
#if defined(__APPLE__)
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

/* Calibration.
 *
 * Times each cell's best variant on a synthetic corpus and divides by the work
 * actually performed, giving ns per unit directly. Deliberately uses the SAME
 * generator as the benchmark so the constants describe the kernels as they are
 * measured, not as a separate microbenchmark imagines them.
 *
 * A density of 0.02 is chosen because it sits near the middle of the band where
 * the cells trade places (results/density.png), so every kernel is exercised on
 * input it is plausibly asked to handle rather than in a regime where it would
 * never be selected.
 *
 * --- CALIBRATE AT THE WORKLOAD'S UNIVERSE, NOT AT 2^14 ----------------------
 *
 * `universe` and `density` are parameters because fixing them at 2^14 and 0.02
 * calibrated every constant in the wrong memory regime. A 2^14-bit row is 2 kB:
 * the whole 48-row corpus is 98 kB and lives in L1. The corpora this model is
 * asked to rank span 2.0e5 to 1.3e8, where one row is 25 kB to 15.8 MB and a
 * bitmap probe is an L2 or DRAM miss rather than an L1 hit.
 *
 * That does not shift the constants uniformly, which is what makes it a
 * ranking error and not just a scale error. B x S pays one random probe per
 * ELEMENT and B x R one per RUN, so the gap between them is a gap in miss
 * counts -- invisible when every probe hits L1, decisive when none do. The
 * model priced B x R at 2x B x S per unit and therefore chose B x S whenever
 * mean run length fell below 2; measured on dimension_008, B x R applied to
 * everything runs 7.36 ns/pair against B x S's 16.41.
 *
 * MEASURED, and it does not work as a drop-in: passing the corpus's own
 * universe and density from bench_allpairs made the model's DECISIONS worse --
 * dimension_008 fell from 0.80x to 0.74x against Roaring and its oracle from
 * 7.72 to 11.03 ns/pair. At a real corpus's shape (3.9e6 bits, density 6.2e-5)
 * the synthetic rows hold ~240 elements, per-call work collapses, fixed
 * overhead dominates the measured time, and dividing by work_units inflates
 * every sparse cell's ns/unit. The regime mismatch is real; fixing it needs a
 * calibration corpus that keeps per-call WORK large while the WORKING SET is
 * large, which is a generator change rather than a parameter change.
 *
 * The parameters stay because that experiment has to be repeatable, and the
 * default reproduces the historical point exactly. Callers should not pass a
 * corpus universe until the generator side is done.
 *
 * Row count is capped so the calibration corpus stays inside kCalibBytes
 * regardless of universe -- at 1.3e8, 48 rows would be 780 MB, which would
 * measure the allocator.
 */
static constexpr size_t kCalibBytes = 192u << 20;   // 192 MB of calibration corpus

void calibrate(CostModel& m, uint32_t universe, double density) {
    default_model(m);                     // sane fallback if anything below fails

    CorpusSpec spec;
    spec.universe = universe ? universe : (1u << 14);
    spec.density  = density > 0.0 ? density : 0.02;
    const size_t row_bytes = (size_t)(spec.universe / 8u) + 1u;
    uint32_t rows = (uint32_t)std::min<size_t>(48, kCalibBytes / row_bytes);
    spec.n_rows   = std::max(8u, rows);   // 8 rows is 28 pairs, enough to time
    spec.seed     = 0xC0571Aull;
    Corpus c;
    generate(c, spec);
    if (c.rows.size() < 2) return;

    struct Pair { uint32_t d, s; };
    std::vector<Pair> pairs;
    for (size_t i = 0; i < c.rows.size(); ++i)
        for (size_t j = i + 1; j < c.rows.size(); ++j) {
            const bool id = c.rows[i].meta.cardinality >= c.rows[j].meta.cardinality;
            pairs.push_back({(uint32_t)(id ? i : j), (uint32_t)(id ? j : i)});
        }

    auto time_cell = [&](Pairing p, auto apply) {
        double work = 0;
        for (const Pair& q : pairs)
            work += work_units(p, c.rows[q.d].meta, c.rows[q.s].meta);
        if (work <= 0) return;

        uint64_t best = UINT64_MAX;
        for (int rep = 0; rep < 3; ++rep) {
            volatile uint64_t sink = 0;
            const uint64_t t0 = cost_now_ns();
            for (int it = 0; it < 4; ++it) {
                uint64_t acc = 0;
                for (const Pair& q : pairs) acc += apply(c.rows[q.d], c.rows[q.s]);
                sink += acc;
            }
            const uint64_t dt = cost_now_ns() - t0;
            (void)sink;
            best = std::min(best, dt);
        }
        m.ns_per_unit[(int)p] = (double)best / (4.0 * work);
    };

    // The best variant of each cell, as measured in round 10.
    auto V = [](auto list, const char* nm) {
        for (size_t i = 0; i < list.n; ++i)
            if (std::string(list.v[i].name) == nm) return list.v[i].fn;
        return list.v[0].fn;
    };
    const auto f_bb = V(cell_bb(), "occ_sel");
    const auto f_bs = V(cell_bs(), "ilp8");
    const auto f_br = V(cell_br(), "hybrid4");
    const auto f_bw = V(cell_bw(), "skip");
    const auto f_ss = V(cell_ss(), "adaptive2");
    const auto f_sr = V(cell_sr(), "adaptive2");
    const auto f_sw = V(cell_sw(), "adaptive");
    const auto f_rr = V(cell_rr(), "adaptive2");
    const auto f_rw = V(cell_rw(), "skip");
    const auto f_ww = V(cell_ww(), "skip2");

    time_cell(Pairing::BB, [&](const Row& a, const Row& b) { return f_bb(a.B(), b.B()); });
    time_cell(Pairing::BS, [&](const Row& a, const Row& b) { return f_bs(a.B(), b.S()); });
    time_cell(Pairing::BR, [&](const Row& a, const Row& b) { return f_br(a.B(), b.R()); });
    time_cell(Pairing::BW, [&](const Row& a, const Row& b) { return f_bw(a.B(), b.W()); });
    time_cell(Pairing::SS, [&](const Row& a, const Row& b) { return f_ss(a.S(), b.S()); });
    time_cell(Pairing::SR, [&](const Row& a, const Row& b) { return f_sr(b.S(), a.R()); });
    time_cell(Pairing::SW, [&](const Row& a, const Row& b) { return f_sw(b.S(), a.W()); });
    time_cell(Pairing::RR, [&](const Row& a, const Row& b) { return f_rr(a.R(), b.R()); });
    time_cell(Pairing::RW, [&](const Row& a, const Row& b) { return f_rw(b.R(), a.W()); });
    time_cell(Pairing::WW, [&](const Row& a, const Row& b) { return f_ww(a.W(), b.W()); });

    /* The two constants calibrate() never measured.
     *
     * ns_fixed and ns_per_occ_word were left at their default_model() values --
     * one host's numbers, hardcoded -- while m.calibrated was set to true. Ten
     * of twelve constants were measured and two were asserted, and nothing said
     * which was which.
     *
     * It matters most exactly where the model is hardest: ns_fixed is added to
     * EVERY cell's prediction, so at 2.0 ns it is larger than the entire cost
     * of a pair on dimension_003 (1.7 ns/pair measured). A fixed term that
     * dominates every candidate equally makes all candidates look equal, which
     * is the selector deciding by tie-break rather than by cost. And
     * ns_per_occ_word is the term C36 added to price B x B's mandatory zone-map
     * scan, so an unmeasured value there reintroduces the mis-pricing C36 was
     * written to remove.
     *
     * ns_fixed is the per-pair floor: loop, orientation, dispatch and the two
     * metadata reads, with the kernel's own work as close to nothing as the
     * harness can make it. Timed on the pair set's cheapest possible call
     * rather than derived as a regression intercept, because an intercept
     * extrapolated from two work levels is a fit, and this is directly
     * observable.
     *
     * ns_per_occ_word is exactly occ_overlap()'s rate: the same loop the B x B
     * kernels run before they can skip anything, over a known number of words. */
    {
        double best = 1e300;
        for (int rep = 0; rep < 3; ++rep) {
            volatile uint64_t sink = 0;
            const uint64_t t0 = cost_now_ns();
            for (int it = 0; it < 16; ++it) {
                uint64_t acc = 0;
                for (const Pair& q : pairs) {
                    // Everything allpairs_sum does per pair except the kernel.
                    const Row& d = c.rows[q.d];
                    const Row& s = c.rows[q.s];
                    acc += d.meta.cardinality + s.meta.n_runs +
                           (d.meta.last_set < s.meta.first_set);
                }
                sink += acc;
            }
            const double dt = (double)(cost_now_ns() - t0);
            (void)sink;
            best = std::min(best, dt);
        }
        m.ns_fixed = best / (16.0 * (double)pairs.size());
    }
    {
        double words = 0;
        for (const Pair& q : pairs)
            words += (double)std::min(c.rows[q.d].occ.size(), c.rows[q.s].occ.size());
        if (words > 0) {
            double best = 1e300;
            for (int rep = 0; rep < 3; ++rep) {
                volatile uint64_t sink = 0;
                const uint64_t t0 = cost_now_ns();
                for (int it = 0; it < 16; ++it) {
                    uint64_t acc = 0;
                    for (const Pair& q : pairs)
                        acc += occ_overlap(c.rows[q.d].B(), c.rows[q.s].B());
                    sink += acc;
                }
                const double dt = (double)(cost_now_ns() - t0);
                (void)sink;
                best = std::min(best, dt);
            }
            m.ns_per_occ_word = best / (16.0 * words);
        }
    }

    /* The scattered-probe cost, measured at both ends of the hierarchy.
     *
     * This replaces a fitted 0.25 that sat inside work_units(). The quantity is
     * real -- B x S pays one bitmap touch per distinct word its sparse side
     * occupies, and that is what makes it lose to a sequential list merge on a
     * scattered row -- but its PRICE is a machine property spanning two orders
     * of magnitude between an L1-resident bitmap and a DRAM-resident one, so a
     * single fitted number is wrong at one end whatever it is.
     *
     * Measured the way the kernel actually probes: word-granular reads at
     * pseudo-random positions, accumulated so nothing is elided. The stride is
     * a large odd multiple of the word size, which defeats the prefetcher
     * without the cost of generating random numbers inside the timed loop. */
    {
        m.l2_bytes = 0;
#if defined(__APPLE__)
        {   size_t v = 0, sz = sizeof(v);
            // perflevel0 is the P-core cluster; its L2 is the one a throughput
            // kernel runs against. Fall back to hw.l2cachesize (E-core on
            // heterogeneous parts, hence the smaller number).
            if (sysctlbyname("hw.perflevel0.l2cachesize", &v, &sz, nullptr, 0) == 0 && v)
                m.l2_bytes = (double)v;
            else if (sysctlbyname("hw.l2cachesize", &v, &sz, nullptr, 0) == 0 && v)
                m.l2_bytes = (double)v;
        }
#endif
        if (m.l2_bytes <= 0) m.l2_bytes = 4.0 * 1024 * 1024;   // labelled guess

        auto probe_rate = [](size_t bytes) {
            const size_t nw = bytes / 8;
            std::vector<uint64_t> buf(nw, 0x5555555555555555ull);
            const size_t stride = 1031;              // odd, prime, prefetch-hostile
            const size_t iters  = 1u << 16;
            volatile uint64_t sink = 0;
            uint64_t acc = 0, i = 0;
            for (size_t k = 0; k < iters; ++k) { i += stride; if (i >= nw) i -= nw;
                                                 acc += STORM_POPCOUNT(buf[i]); }
            sink += acc;
            double best = 1e300;
            for (int rep = 0; rep < 3; ++rep) {
                acc = 0; i = 0;
                const uint64_t t0 = cost_now_ns();
                for (size_t k = 0; k < iters; ++k) { i += stride; if (i >= nw) i -= nw;
                                                     acc += STORM_POPCOUNT(buf[i]); }
                const double dt = (double)(cost_now_ns() - t0);
                sink += acc;
                if (dt < best) best = dt;
            }
            (void)sink;
            return best / (double)iters;
        };
        // Hot: a quarter of L2, comfortably resident. Cold: 8x L2, comfortably not.
        m.ns_probe_hot  = probe_rate((size_t)(m.l2_bytes / 4));
        m.ns_probe_cold = probe_rate((size_t)(m.l2_bytes * 8));
    }

    m.calibrated = true;
}

/* See storm_cost.h. Times each cell on a stride-sampled set of the caller's own
 * pairs and divides by the work those pairs represent.
 *
 * The pair sample STRIDES the upper triangle rather than truncating it. Row
 * order is never arbitrary -- graph vertex ids follow crawl order, variants
 * follow genomic position -- so the first k pairs are one neighbourhood, not a
 * sample of the corpus. That mistake has already invalidated one measurement
 * campaign here. */
void calibrate_on_rows(CostModel& m, const std::vector<Row>& rows,
                       uint32_t max_pairs, double budget_ns)
{
    if (rows.size() < 2) return;
    const uint64_t n = rows.size();
    const uint64_t T = n * (n - 1) / 2;
    const uint64_t target = std::min<uint64_t>(max_pairs, T);
    if (!target) return;
    const uint64_t step = std::max<uint64_t>(1, T / target);

    struct Pair { uint32_t d, s; };
    std::vector<Pair> pairs;
    pairs.reserve(target);
    for (uint64_t k = 0; k < T && pairs.size() < target; k += step) {
        const double b = (double)(2 * n - 1);
        uint64_t i = (uint64_t)((b - std::sqrt(b * b - 8.0 * (double)k)) / 2.0);
        while (i + 1 < n && (i + 1) * (2 * n - i - 2) / 2 <= k) ++i;
        while (i > 0    && i * (2 * n - i - 1) / 2 >  k)      --i;
        const uint64_t j = k - i * (2 * n - i - 1) / 2 + i + 1;
        if (j <= i || j >= n) continue;
        /* Span-disjoint pairs must NOT calibrate a kernel.
         *
         * allpairs_sum settles them with the O(1) span test before any kernel
         * runs, so they are not part of the population any constant describes.
         * Including them is not merely imprecise, it inverts the ranking: a
         * merge kernel handed two non-overlapping ranges finishes almost
         * instantly while work_units still charges it ra+rb, so its ns/unit is
         * deflated in proportion to how many disjoint pairs the corpus has.
         * On census1881, where 63.3% of pairs are disjoint, that put R x R at
         * 0.199 ns/unit against the synthetic corpus's 2.046 -- a 10x
         * under-price -- the selector duly routed 9% of pairs to R x R, and the
         * corpus went from 0.92x against Roaring to 0.07x. */
        const RowMeta& mi = rows[i].meta;
        const RowMeta& mj = rows[j].meta;
        if (!mi.cardinality || !mj.cardinality ||
            mi.last_set < mj.first_set || mj.last_set < mi.first_set) continue;
        const bool id = mi.cardinality >= mj.cardinality;
        pairs.push_back({(uint32_t)(id ? i : j), (uint32_t)(id ? j : i)});
    }
    if (pairs.empty()) return;

    auto V = [](auto list, const char* nm) {
        for (size_t i = 0; i < list.n; ++i)
            if (std::string(list.v[i].name) == nm) return list.v[i].fn;
        return list.v[0].fn;
    };
    const auto f_bb = V(cell_bb(), "occ_sel");
    const auto f_bs = V(cell_bs(), "ilp8");
    const auto f_br = V(cell_br(), "hybrid4");
    const auto f_bw = V(cell_bw(), "skip");
    const auto f_ss = V(cell_ss(), "adaptive2");
    const auto f_sr = V(cell_sr(), "adaptive2");
    const auto f_rr = V(cell_rr(), "adaptive2");
    const auto f_ww = V(cell_ww(), "skip2");

    /* Total work over the sample, computed once: the ratio's denominator does
     * not depend on how many times the numerator is measured. */
    auto sample_work = [&](Pairing p) {
        double w = 0;
        for (const Pair& q : pairs) {
            const RowMeta& md = rows[q.d].meta;
            const RowMeta& ms = rows[q.s].meta;
            w += (p == Pairing::BB && !(md.has_occ && ms.has_occ))
               ? (double)std::max(md.n_words, ms.n_words)
               : work_units(p, md, ms);
        }
        return w;
    };

    /* Time the WHOLE sample under one clock reading, warm, and take the min of
     * repeats.
     *
     * The first version warmed a single pair and then called cost_now_ns()
     * around every individual kernel call. Both are wrong at these working-set
     * sizes. A census1881 row is 535 kB and the corpus is 200 MB, so 255 of the
     * 256 sampled pairs were measuring cold first-touch -- page faults and DRAM
     * fills for rows built moments earlier -- and on top of that each ~185 ns
     * kernel carried ~25 ns of clock overhead. B x S calibrated at 1.005
     * ns/element where the benchmark measures 0.037, a 27x over-price, and the
     * selector abandoned B x S entirely.
     *
     * This is the same warm-up-then-repeat-to-a-floor discipline
     * bench_allpairs.cpp already applies to the policies themselves; the
     * calibration had been exempt from it. */
    /* Warm, whole-sample, min-of-repeats. Ratio of summed time to summed work.
     *
     * A median-of-quartile-slopes variant was tried, on the theory that a ratio
     * of sums is dominated by the largest pairs and so mis-ranks the rest. It
     * was worse on every corpus that moved (census1881 0.79x -> 0.41x,
     * weather_sept_85 1.07x -> 0.37x, wikileaks 3.56x -> 2.00x). Recorded so it
     * is not tried a fourth time. */
    auto time_cell = [&](Pairing p, auto apply) {
        const double work = sample_work(p);
        if (work <= 0) return;
        volatile uint64_t sink = 0;
        for (const Pair& q : pairs) sink += apply(rows[q.d], rows[q.s]);  // warm
        double best = 1e300, spent = 0;
        for (int rep = 0; rep < 5 && spent < budget_ns; ++rep) {
            const uint64_t t0 = cost_now_ns();
            for (const Pair& q : pairs) sink += apply(rows[q.d], rows[q.s]);
            const double dt = (double)(cost_now_ns() - t0);
            spent += dt;
            if (dt < best) best = dt;
        }
        (void)sink;
        m.ns_per_unit[(int)p] = best / work;
    };

    time_cell(Pairing::BB, [&](const Row& a, const Row& b) { return f_bb(a.B(), b.B()); });
    time_cell(Pairing::BS, [&](const Row& a, const Row& b) { return f_bs(a.B(), b.S()); });
    time_cell(Pairing::BR, [&](const Row& a, const Row& b) { return f_br(a.B(), b.R()); });
    time_cell(Pairing::BW, [&](const Row& a, const Row& b) { return f_bw(a.B(), b.W()); });
    time_cell(Pairing::SS, [&](const Row& a, const Row& b) { return f_ss(a.S(), b.S()); });
    time_cell(Pairing::SR, [&](const Row& a, const Row& b) { return f_sr(b.S(), a.R()); });
    time_cell(Pairing::RR, [&](const Row& a, const Row& b) { return f_rr(a.R(), b.R()); });
    time_cell(Pairing::WW, [&](const Row& a, const Row& b) { return f_ww(a.W(), b.W()); });
}

} // namespace storm
