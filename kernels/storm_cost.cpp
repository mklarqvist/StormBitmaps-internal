/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist
 * Licensed under the Apache License, Version 2.0.
 */
#include "kernels/storm_cost.h"
#include "kernels/storm_cells.h"
#include "kernels/storm_gen.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <time.h>

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

static double work_units(Pairing p, const RowMeta& a, const RowMeta& b) {
    const double m  = (double)std::max(a.n_words, b.n_words);
    const double sa = a.cardinality, sb = b.cardinality;
    const double ra = a.n_runs,      rb = b.n_runs;
    // EWAH stream length is not in RowMeta; runs bound it closely -- a row with
    // r runs has at most 2r+1 markers plus its literals -- so runs stand in for
    // it. Approximation, and flagged as one.
    const double wa = 2.0 * ra + 1.0, wb = 2.0 * rb + 1.0;

    switch (p) {
        case Pairing::BB: return bb_expected_bins(a, b);
        case Pairing::BS: return std::min(sa, sb);
        case Pairing::BR: return std::min(ra, rb);
        case Pairing::BW: return std::min(wa, wb);
        // Integer log2, NOT std::log2. The first version of this called the
        // libm double routine inside the selection loop and selection cost
        // measured 24 ns/pair -- 36% of runtime against a 2% gate. Standing
        // rule 7 says the decision must be near-free, and a transcendental in
        // the hot path is the opposite of that.
        case Pairing::SS: {
            const uint32_t mx = (uint32_t)std::max(2.0, std::max(sa, sb));
            return std::min(sa, sb) * (double)(32 - __builtin_clz(mx));
        }
        case Pairing::SR: return std::min(sa + rb, sb + ra);
        case Pairing::SW: return std::min(sa + wb, sb + wa);
        case Pairing::RR: return ra + rb;
        case Pairing::RW: return std::min(ra + wb, rb + wa);
        case Pairing::WW: return wa + wb;
        default:          return 0.0;
    }
}

double predict(const CostModel& m, Pairing p, const RowMeta& a, const RowMeta& b) {
    if (p == Pairing::Empty) return 0.0;
    return m.ns_fixed + m.ns_per_unit[(int)p] * work_units(p, a, b);
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
 */
void calibrate(CostModel& m) {
    default_model(m);                     // sane fallback if anything below fails

    CorpusSpec spec;
    spec.n_rows   = 48;
    spec.universe = 1u << 14;
    spec.density  = 0.02;
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

    m.calibrated = true;
}

} // namespace storm
