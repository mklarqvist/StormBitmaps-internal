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

    bool calibrated = false;
};

// Run the calibration microbenchmarks. Costs a few hundred ms; done once.
void calibrate(CostModel& m);

// A hardcoded model, for builds that cannot afford startup calibration. These
// are this host's measured values and are WRONG on any other machine -- which
// is the entire argument for calibrating instead. Labelled, not hidden.
void default_model(CostModel& m);

// Predicted cost in nanoseconds of computing |A ∩ B| via `p`.
double predict(const CostModel& m, Pairing p, const RowMeta& a, const RowMeta& b);

// The selection decision (M2). Returns the cheapest pairing and, optionally,
// its predicted cost. Reads only metadata; touches no row data.
Pairing select_pairing(const CostModel& m, const RowMeta& a, const RowMeta& b,
                       double* predicted = nullptr);

} // namespace storm

#endif // STORM_COST_H_
