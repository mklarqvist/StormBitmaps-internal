/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist
 * Licensed under the Apache License, Version 2.0.
 *
 * M4 — the calibrated cost model (RESEARCH_PLAN.md 5.1).
 *
 * WHY THIS EXISTS, measured rather than argued. Round 10 of the kernel campaign
 * did nothing but sample the hardcoded constants -- gallop ratios, rank
 * crossovers, fill lengths, zone-map selectivity -- and changed the winner in
 * seven of ten cells. The margins were mostly within noise, but two were not,
 * and more importantly the sampled optimum DIFFERED BY CORPUS every time. There
 * is no single right constant, so no amount of hand-tuning finds one.
 *
 * That is the case for a cost model stated as a measurement (OPTLOG.md F10).
 *
 * The model has three parts:
 *
 *   1. CALIBRATION. Time each cell's kernel on synthetic inputs at build or
 *      startup, once, and record nanoseconds per unit of that cell's work. The
 *      constants are properties of the machine, not of the algorithm.
 *
 *   2. PREDICTION. Given per-row metadata (M1) predict each candidate pairing's
 *      cost from its own asymptotics: Theta(m) for B x B, Theta(|S|) for B x S,
 *      Theta(r) for B x R, and so on.
 *
 *   3. SELECTION. Take the cheapest. This is M2, and standing rule 7 requires
 *      it to be near-free: everything here is arithmetic on six integers
 *      already computed at build time. No data is touched.
 *
 * The metric that makes this a contribution is REGRET, not speed: how much
 * slower the model's choice is than an oracle that always picks correctly
 * (RESEARCH_PLAN.md 5.1, "Report the model's regret"). A model that is fast but
 * frequently wrong is worse than one that is slightly slower and always right,
 * because the regret is what generalizes to machines we have not measured.
 */
#ifndef STORM_COST_H_
#define STORM_COST_H_

#include <cstdint>
#include "kernels/storm_repr.h"

namespace storm {

// The pairings the model can choose between. Ro is absent for the same reason
// it is absent from the kernel layer: it is a meta-representation and belongs
// above this decision, not inside it.
enum class Pairing : uint8_t {
    BB, BS, BR, BW, SS, SR, SW, RR, RW, WW, Empty, COUNT
};

const char* name_of(Pairing p);

/* Calibrated nanoseconds per unit of work, one per cell.
 *
 * Units differ by cell and are the cell's own asymptotic driver -- words for
 * B x B, list elements for B x S, runs for B x R. That is what makes the
 * constants comparable across machines: they are the slope of each kernel's
 * cost curve, with the shape factored out into the prediction step.
 */
struct CostModel {
    double ns_per_unit[(int)Pairing::COUNT] = {0};

    // Fixed per-pair overhead: call, metadata read, selection arithmetic.
    // Measured, because at 10^10 pairs a 2 ns constant is not a rounding error.
    double ns_fixed = 0;

    // The zone-map planning step, in ns per summary word. Charged separately
    // because it is paid BEFORE the kernel choice and is what makes a disjoint
    // pair free.
    double ns_per_occ_word = 0;

    /* Cost of ONE scattered probe into the dense side's bitmap, which is what
     * B x S pays per distinct word its sparse side occupies.
     *
     * Two values because it is not one number. A probe into a 25 kB bitmap is
     * an L1 hit and a probe into a 15.8 MB one is a DRAM round trip, and the
     * corpora here span exactly that range -- so a single constant is wrong at
     * one end whatever it is set to. predict() picks by the dense row's bitmap
     * size, which it already has in n_words.
     *
     * This lives in CostModel rather than in work_units() because it is a
     * property of the MACHINE, which is the separation this header opens by
     * claiming: "the work function is a property of the algorithm and is the
     * same everywhere, while ns_per_unit is a property of the machine". The
     * first version of this term violated that -- a fitted 0.25 sitting inside
     * work_units(), tier 2 on the evidence ladder, correct only on this host. */
    double ns_probe_hot  = 0;   // bitmap fits in L2
    double ns_probe_cold = 0;   // bitmap exceeds L2
    double l2_bytes      = 0;   // measured, not assumed

    bool calibrated = false;
};

// Run the calibration microbenchmarks. Costs a few hundred ms; done once.
/* Measure the constants on THIS host, for the universe and density the caller
 * is about to run. Both parameters matter: at 2^14 bits every bitmap probe is
 * an L1 hit and at 1.3e8 none of them are, so the ratio between a per-element
 * cell and a per-run cell -- which is a ratio of MISS counts -- is not the same
 * number in the two regimes. The defaults reproduce the historical calibration
 * point and are the right choice only for a cache-resident workload. */
void calibrate(CostModel& m, uint32_t universe = 1u << 14, double density = 0.02);

/* Calibrate on the CALLER'S OWN ROWS -- the offline corpus optimiser.
 *
 * calibrate() times a synthetic corpus, and two attempts to make that
 * representative have now failed in opposite directions: at the default 2^14
 * bits it measures an L1-resident workload the real corpora never resemble, and
 * at a real corpus's universe and density the synthetic rows are too sparse for
 * per-call work to exceed fixed overhead, so every constant inflates.
 *
 * The way out is to stop synthesising. The rows are already built and the pair
 * distribution is already known, so sample real pairs, time each cell on them,
 * and divide by the work those pairs actually represent. No generator, no
 * shape assumption, and the memory regime is the deployment's by construction.
 *
 * This is a build-time step, not a query-time one -- the same offline
 * optimisation Roaring performs with run_optimize(). Bounded by `budget_ns` per
 * cell so it stays negligible against N^2: a cell that is slow on this corpus
 * gets fewer sample pairs rather than more time, which is the correct trade
 * because a slow cell's constant is easy to estimate from few samples.
 *
 * Falls back to the measured-synthetic constant for any cell whose sampled work
 * is zero (a corpus with no runs cannot calibrate B x R).
 *
 * --- OFF BY DEFAULT: A NET LOSS, IN ALL THREE VARIANTS TRIED ----------------
 *
 * Enable with STORM_ROWCAL=1 in bench_allpairs. Measured against the synthetic
 * baseline, per-tile vs fixed Roaring, medians of three:
 *
 *                    baseline   rowcal(v3)
 *   dimension_008      0.93x      1.24x     <- helps
 *   soc-Pokec          2.32x      2.28x
 *   weather_sept_85    1.07x      1.07x
 *   as-skitter         1.12x      0.95x
 *   census1881         0.92x      0.79x
 *   dimension_033      3.48x      1.53x     <- hurts badly
 *
 * v1 (per-pair clock, one warm-up pair) put census1881 at 0.07x: it measured
 * cold first-touch of 535 kB rows plus ~25 ns of clock overhead on a ~185 ns
 * kernel, over-pricing B x S by 27x. v2 (whole-sample, warm, min-of-repeats)
 * is the version kept here. v3, a median-of-quartile-slopes intended to stop
 * the ratio of sums being dominated by the largest pairs, was worse again
 * (census1881 0.41x, weather_sept_85 0.37x, wikileaks 3.56x -> 2.00x).
 *
 * The pattern across all three: measuring on real data makes predictions more
 * accurate and SELECTION worse. That is diagnostic. ns_per_unit is one scalar
 * per cell, and on a real corpus it absorbs memory-hierarchy effects that
 * work_units() does not model -- so the constant stops being a property of the
 * kernel and becomes a property of one corpus's size distribution, which is
 * precisely what a slope is supposed to factor out. The synthetic constant is
 * worse per pair and better at ranking because it is closer to the kernel's
 * intrinsic cost.
 *
 * So calibration is the wrong lever. The right one is a work_units() that
 * models the hierarchy -- charging B x S by DISTINCT CACHE LINES touched
 * rather than by elements, which is the quantity that actually diverges
 * between an L1-resident universe and a 1.3e8-bit one. That is a model change,
 * and it is the open item. */
void calibrate_on_rows(CostModel& m, const std::vector<Row>& rows,
                       uint32_t max_pairs = 256, double budget_ns = 2e6);

// A hardcoded model, for builds that cannot afford startup calibration. These
// are this host's measured values and are WRONG on any other machine -- which
// is the entire argument for calibrating instead. Labelled, not hidden.
/* THE variant name for a cell -- the single source of truth.
 *
 * calibrate() decides what gets TIMED and storm_allpairs.cpp's Kernels decides
 * what gets RUN, and they used to hold independent hardcoded lists that agreed
 * only because both were edited to. Any variant override then changed the
 * kernel while leaving the constant that prices it describing a different one,
 * which silently invalidates variant sweeps and is the same model-vs-code
 * mismatch that made B x R and B x W a coin flip. Both now call this.
 *
 * `cell` is the two-letter tag ("bs", "br", "rr", ...); STORM_VARIANT_<CELL>
 * overrides. */
const char* chosen_variant(const char* cell, const char* dflt);

void default_model(CostModel& m);

// Predicted cost in nanoseconds of computing |A ∩ B| via `p`.
double predict(const CostModel& m, Pairing p, const RowMeta& a, const RowMeta& b);

// The selection decision (M2). Returns the cheapest pairing and, optionally,
// its predicted cost. Reads only metadata; touches no row data.
Pairing select_pairing(const CostModel& m, const RowMeta& a, const RowMeta& b,
                       double* predicted = nullptr);

} // namespace storm

#endif // STORM_COST_H_
