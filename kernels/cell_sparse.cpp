/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist
 * Licensed under the Apache License, Version 2.0.
 *
 * The sparse-side cells: S x S, S x R, R x R.
 *
 * S x S is the one cell of the pairing matrix with mature prior art — Lemire's
 * SIMDCompressionAndIntersection and Roaring's array containers both solve it,
 * and LANDSCAPE.md records that. It is here so the matrix is complete and so
 * the selection model has a calibrated cost for it, not because a new S x S
 * kernel is a contribution.
 *
 * S x R is P0 and genuinely unaddressed: it is the pairing you get when a
 * singleton row meets a clustered row, which on a 1/i spectrum is common.
 */
#include "kernels/storm_cells.h"

#include <algorithm>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#  include <arm_neon.h>
#  define STORM_CELL_NEON 1
#else
#  define STORM_CELL_NEON 0
#endif

namespace storm {
namespace {

// ===========================================================================
// S x S
// ===========================================================================

uint64_t ss_merge(const ListView& a, const ListView& b) {
    uint64_t c = 0;
    uint32_t i = 0, j = 0;
    while (i < a.n && j < b.n) {
        if      (a.v[i] < b.v[j]) ++i;
        else if (a.v[i] > b.v[j]) ++j;
        else { ++c; ++i; ++j; }
    }
    return c;
}

// Branchless merge. Every comparison in ss_merge is a data-dependent branch and
// on interleaved data it mispredicts about half the time; this form pays a
// fixed cost per step instead.
uint64_t ss_merge_bl(const ListView& a, const ListView& b) {
    uint64_t c = 0;
    uint32_t i = 0, j = 0;
    while (i < a.n && j < b.n) {
        const uint32_t va = a.v[i], vb = b.v[j];
        c += (va == vb);
        i += (va <= vb);
        j += (vb <= va);
    }
    return c;
}

// Galloping. When one side is far shorter — which under a 1/i spectrum is the
// normal case, not the exception — a merge wastes Theta(|long|) steps. Galloping
// is Theta(|short| * log(|long| / |short|)).
static inline uint32_t gallop(const uint32_t* v, uint32_t n, uint32_t lo, uint32_t key) {
    uint32_t step = 1;
    uint32_t hi;
    while (lo + step < n && v[lo + step] < key) { lo += step; step <<= 1; }
    hi = std::min(lo + step, n);
    // v[lo] < key <= v[hi] (or hi == n)
    while (lo < hi) {
        const uint32_t mid = lo + ((hi - lo) >> 1);
        if (v[mid] < key) lo = mid + 1; else hi = mid;
    }
    return lo;
}

uint64_t ss_gallop(const ListView& a, const ListView& b) {
    const ListView& s = (a.n <= b.n) ? a : b;
    const ListView& l = (a.n <= b.n) ? b : a;
    uint64_t c = 0;
    uint32_t p = 0;
    for (uint32_t i = 0; i < s.n && p < l.n; ++i) {
        p = gallop(l.v, l.n, p, s.v[i]);
        if (p < l.n && l.v[p] == s.v[i]) { ++c; ++p; }
    }
    return c;
}

#if STORM_CELL_NEON
// Block-wise all-pairs compare, in the spirit of Lemire's SIMD list
// intersection: compare 4 elements of A against 4 of B by rotating one side
// three times, then advance whichever block has the smaller maximum. Four
// vceqq_u32 + three rotations replace up to 16 scalar comparisons.
uint64_t ss_neon(const ListView& a, const ListView& b) {
    uint64_t c = 0;
    uint32_t i = 0, j = 0;
    if (a.n >= 4 && b.n >= 4) {
        while (i + 4 <= a.n && j + 4 <= b.n) {
            const uint32x4_t va = vld1q_u32(a.v + i);
            uint32x4_t vb = vld1q_u32(b.v + j);

            uint32x4_t m = vceqq_u32(va, vb);
            vb = vextq_u32(vb, vb, 1); m = vorrq_u32(m, vceqq_u32(va, vb));
            vb = vextq_u32(vb, vb, 1); m = vorrq_u32(m, vceqq_u32(va, vb));
            vb = vextq_u32(vb, vb, 1); m = vorrq_u32(m, vceqq_u32(va, vb));

            // Each matching lane is all-ones; -(sum of lanes as signed) counts them.
            c += vaddvq_u32(vshrq_n_u32(m, 31));

            const uint32_t amax = a.v[i + 3], bmax = b.v[j + 3];
            i += (amax <= bmax) ? 4 : 0;
            j += (bmax <= amax) ? 4 : 0;
        }
    }
    while (i < a.n && j < b.n) {
        const uint32_t va = a.v[i], vb = b.v[j];
        c += (va == vb);
        i += (va <= vb);
        j += (vb <= va);
    }
    return c;
}
#endif

// Size-adaptive: merge when the sides are comparable, gallop when they are not.
// The ratio is the same kind of threshold M4 is supposed to own; it is named
// here rather than buried as a literal.
constexpr uint32_t kGallopRatio = 8;

uint64_t ss_adaptive(const ListView& a, const ListView& b) {
    const uint32_t lo = std::min(a.n, b.n), hi = std::max(a.n, b.n);
    if (lo == 0) return 0;
    if (hi / lo >= kGallopRatio) return ss_gallop(a, b);
#if STORM_CELL_NEON
    return ss_neon(a, b);          // 2.39x over the merge on balanced pairs
#else
    return ss_merge_bl(a, b);
#endif
}

// ===========================================================================
// S x R  — P0
// ===========================================================================

// Theta(|S| + r). Each step either consumes a list element or retires a run.
uint64_t sr_merge(const ListView& s, const RunView& r) {
    uint64_t c = 0;
    uint32_t i = 0, j = 0;
    while (i < s.n && j < r.n) {
        if      (s.v[i] <  r.start[j]) ++i;
        else if (s.v[i] >= r.end[j])   ++j;
        else { ++c; ++i; }
    }
    return c;
}

// Branchless merge. `s.v[i] >= r.start[j]` and `s.v[i] < r.end[j]` are two
// independent predicates, so the whole step reduces to two comparisons and two
// conditional increments with no control flow.
uint64_t sr_merge_bl(const ListView& s, const RunView& r) {
    uint64_t c = 0;
    uint32_t i = 0, j = 0;
    while (i < s.n && j < r.n) {
        const uint32_t v = s.v[i];
        const bool before = v <  r.start[j];
        const bool after  = v >= r.end[j];
        c += (!before && !after);
        i += !after;          // consume the element unless it is past this run
        j +=  after;          // retire the run when the element is past it
    }
    return c;
}

// Binary search per list element. Theta(|S| log r), which beats the merge when
// the list is much shorter than the run array — the singleton-against-clustered
// pairing that a 1/i spectrum produces constantly.
uint64_t sr_search(const ListView& s, const RunView& r) {
    uint64_t c = 0;
    uint32_t p = 0;
    for (uint32_t i = 0; i < s.n && p < r.n; ++i) {
        p = gallop(r.end, r.n, p, s.v[i] + 1);   // first run with end > v
        if (p < r.n && s.v[i] >= r.start[p]) ++c;
    }
    return c;
}

/* Search from the RUN side. For each run, the number of list elements inside it
 * is the difference of two binary searches:
 *
 *     count_i = lower_bound(S, end_i) - lower_bound(S, start_i)
 *
 * so the cell costs Theta(r log |S|). This is the mirror of sr_search and it is
 * the variant the baseline sweep showed to be missing: on a long-run corpus
 * (r = 3.3, |S| = 13107) every existing S x R variant walked all 13,107 list
 * elements and took 8.5-18.8 microseconds per pair, when the answer is six
 * binary searches. Having only sr_search meant S x R could exploit a short LIST
 * but not a short RUN ARRAY -- an asymmetry with no justification, since the
 * whole premise of the pairing matrix is that either side may be the sparse one.
 */
uint64_t sr_search_runs(const ListView& s, const RunView& r) {
    uint64_t c = 0;
    uint32_t p = 0;
    for (uint32_t i = 0; i < r.n && p < s.n; ++i) {
        const uint32_t lo = gallop(s.v, s.n, p,  r.start[i]);
        const uint32_t hi = gallop(s.v, s.n, lo, r.end[i]);
        c += hi - lo;
        p = hi;
    }
    return c;
}

/* Three-way selection. The cost of each strategy is roughly
 *
 *     merge        |S| + r
 *     search       |S| log r        (short list, many runs)
 *     search_runs  r log |S|        (few runs, long list)
 *
 * so the decision is a direct comparison of those three, computed from the two
 * sizes alone -- O(1) metadata, which is what standing rule 7 requires of a
 * per-pair decision. This replaces a single hardcoded ratio that got the
 * long-run corpus exactly backwards: it read r/|S| = 0, concluded "merge", and
 * picked the branchless merge, which was the slowest of the four. */
static inline uint32_t ilog2_up(uint32_t x) {
    return x <= 1 ? 1u : (uint32_t)(32 - __builtin_clz(x - 1));
}

uint64_t sr_adaptive(const ListView& s, const RunView& r) {
    if (s.n == 0 || r.n == 0) return 0;
    const uint64_t c_merge  = (uint64_t)s.n + r.n;
    const uint64_t c_search = (uint64_t)s.n * ilog2_up(r.n);
    const uint64_t c_runs   = (uint64_t)r.n * ilog2_up(s.n);
    if (c_runs <= c_search && c_runs <= c_merge)   return sr_search_runs(s, r);
    if (c_search < c_merge)                        return sr_search(s, r);
    return sr_merge(s, r);
}

// ===========================================================================
// R x R
// ===========================================================================

// Overlap of [sa, ea) and [sb, eb) is max(0, min(ea,eb) - max(sa,sb)).
uint64_t rr_merge(const RunView& a, const RunView& b) {
    uint64_t c = 0;
    uint32_t i = 0, j = 0;
    while (i < a.n && j < b.n) {
        const uint32_t lo = std::max(a.start[i], b.start[j]);
        const uint32_t hi = std::min(a.end[i],   b.end[j]);
        if (hi > lo) c += hi - lo;
        if (a.end[i] < b.end[j]) ++i; else ++j;
    }
    return c;
}

uint64_t rr_merge_bl(const RunView& a, const RunView& b) {
    uint64_t c = 0;
    uint32_t i = 0, j = 0;
    while (i < a.n && j < b.n) {
        const uint32_t ea = a.end[i], eb = b.end[j];
        const uint32_t lo = std::max(a.start[i], b.start[j]);
        const uint32_t hi = std::min(ea, eb);
        c += (hi > lo) ? (hi - lo) : 0;
        i += (ea <= eb);
        j += (eb <= ea);
    }
    return c;
}

// Galloping over runs: skip whole stretches of A that end before B's current
// run begins. On heavily skewed pairs this is the difference between
// Theta(r_A + r_B) and Theta(r_min log r_max).
uint64_t rr_gallop(const RunView& a, const RunView& b) {
    const RunView& s = (a.n <= b.n) ? a : b;
    const RunView& l = (a.n <= b.n) ? b : a;
    uint64_t c = 0;
    uint32_t p = 0;
    for (uint32_t i = 0; i < s.n && p < l.n; ++i) {
        p = gallop(l.end, l.n, p, s.start[i] + 1);   // first long run ending past s.start
        for (uint32_t q = p; q < l.n && l.start[q] < s.end[i]; ++q) {
            const uint32_t lo = std::max(s.start[i], l.start[q]);
            const uint32_t hi = std::min(s.end[i],   l.end[q]);
            if (hi > lo) c += hi - lo;
        }
    }
    return c;
}

constexpr uint32_t kRrGallopRatio = 8;

uint64_t rr_adaptive(const RunView& a, const RunView& b) {
    const uint32_t lo = std::min(a.n, b.n), hi = std::max(a.n, b.n);
    if (lo == 0) return 0;
    return (hi / lo >= kRrGallopRatio) ? rr_gallop(a, b) : rr_merge_bl(a, b);
}

// ---------------------------------------------------------------------------

const Variant<fn_ss> kSS[] = {
    {"merge",     ss_merge,     "reference: branchy sorted merge"},
    {"merge_bl",  ss_merge_bl,  "branchless merge"},
    {"gallop",    ss_gallop,    "exponential + binary search from the shorter side"},
    {"adaptive",  ss_adaptive,  "merge or gallop on the length ratio"},
#if STORM_CELL_NEON
    {"neon",      ss_neon,      "4x4 block compare via vextq rotation"},
#endif
};

const Variant<fn_sr> kSR[] = {
    {"merge",     sr_merge,     "reference: Theta(|S| + r) merge"},
    {"merge_bl",  sr_merge_bl,  "branchless merge"},
    {"search",    sr_search,    "gallop into the run array per list element"},
    {"search_runs", sr_search_runs, "two galloping searches into the LIST per run"},
    {"adaptive",  sr_adaptive,  "three-way cost comparison from the two sizes"},
};

const Variant<fn_rr> kRR[] = {
    {"merge",     rr_merge,     "reference: overlap merge"},
    {"merge_bl",  rr_merge_bl,  "branchless merge"},
    {"gallop",    rr_gallop,    "gallop from the shorter run array"},
    {"adaptive",  rr_adaptive,  "merge or gallop on the run-count ratio"},
};

} // namespace

VariantList<fn_ss> cell_ss() { return {kSS, sizeof(kSS) / sizeof(kSS[0])}; }
VariantList<fn_sr> cell_sr() { return {kSR, sizeof(kSR) / sizeof(kSR[0])}; }
VariantList<fn_rr> cell_rr() { return {kRR, sizeof(kRR) / sizeof(kRR[0])}; }

} // namespace storm
