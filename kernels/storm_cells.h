/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist
 * Licensed under the Apache License, Version 2.0.
 *
 * The pairing matrix — one cell per pair of representations
 * (PROBLEM_STATEMENT.md 3, RESEARCH_PLAN.md 1).
 *
 * Every cell exposes a list of interchangeable VARIANTS behind one signature.
 * A variant is a complete implementation of that cell; the harness races them
 * against each other and against the cell's designated reference. Because they
 * share a signature they also share the oracle, so a variant that is faster and
 * wrong cannot be reported as faster.
 *
 * Cells are ordered by the priority of RESEARCH_PLAN.md 1 — the asymmetric
 * cells first, B x B last, because B x B is ~1% of pairs on skewed data.
 */
#ifndef STORM_CELLS_H_
#define STORM_CELLS_H_

#include <cstdint>
#include "kernels/storm_repr.h"

namespace storm {

// --- Cell signatures -------------------------------------------------------
// Convention: the argument order is the cell name. B x S takes (bitmap, list).
// A kernel must NOT assume which side is sparser; the selection layer (M2) is
// responsible for orienting a pair before the call, and the harness feeds
// kernels in both orientations to keep them honest about it.
using fn_bb = uint64_t (*)(const BitmapView&, const BitmapView&);
using fn_bs = uint64_t (*)(const BitmapView&, const ListView&);
using fn_br = uint64_t (*)(const BitmapView&, const RunView&);
using fn_bw = uint64_t (*)(const BitmapView&, const EwahView&);
using fn_ss = uint64_t (*)(const ListView&,   const ListView&);
using fn_sr = uint64_t (*)(const ListView&,   const RunView&);
using fn_sw = uint64_t (*)(const ListView&,   const EwahView&);
using fn_rr = uint64_t (*)(const RunView&,    const RunView&);
using fn_rw = uint64_t (*)(const RunView&,    const EwahView&);
using fn_ww = uint64_t (*)(const EwahView&,   const EwahView&);

// --- Variant descriptor ----------------------------------------------------
template <typename F>
struct Variant {
    const char* name;
    F           fn;
    const char* note;
    // Set when the kernel reads BitmapView::rank. Such a variant is only
    // comparable to a rank-free one once the index build cost is accounted for,
    // so the harness reports it in a separate column rather than silently
    // crowning it (RESEARCH_PLAN.md 4.5, "when is building the index worth it?").
    bool        needs_rank = false;
    // Set when the kernel allocates or writes scratch proportional to the
    // universe (the inflate-to-bitmap baselines). Standing rule 3: permitted
    // only as an explicitly labelled baseline.
    bool        inflates   = false;
};

template <typename F>
struct VariantList {
    const Variant<F>* v;
    size_t            n;
};

// --- Per-cell registries ---------------------------------------------------
VariantList<fn_bb> cell_bb();
VariantList<fn_bs> cell_bs();
VariantList<fn_br> cell_br();
VariantList<fn_bw> cell_bw();
VariantList<fn_ss> cell_ss();
VariantList<fn_sr> cell_sr();
VariantList<fn_sw> cell_sw();
VariantList<fn_rr> cell_rr();
VariantList<fn_rw> cell_rw();
VariantList<fn_ww> cell_ww();

// --- Oracle ----------------------------------------------------------------
// Deliberately the dumbest possible implementation: decode both sides to dense
// bitmaps and count. Slow, obviously correct, and shares no code with any
// kernel, which is the only property that matters here.
uint64_t oracle_intersect(const Row& a, const Row& b);

} // namespace storm

#endif // STORM_CELLS_H_
