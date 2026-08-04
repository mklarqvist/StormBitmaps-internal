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

#if STORM_CELL_NEON
// Forward declarations: the second-generation selectors are defined next to the
// first-generation ones for readability, but dispatch to kernels defined below.
uint64_t ss_neon8(const ListView&, const ListView&);
#endif

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

// Symmetric galloping: after each match or mismatch, gallop the side that is
// BEHIND rather than always driving from the shorter list. ss_gallop fixes the
// driving side up front, which is right when the length ratio is extreme and
// wasteful when the lists are similar in length but occupy different parts of
// the universe -- exactly what a 1/i spectrum produces, where two rows may have
// comparable cardinality and almost no overlap.
uint64_t ss_gallop_sym(const ListView& a, const ListView& b) {
    uint64_t c = 0;
    uint32_t i = 0, j = 0;
    while (i < a.n && j < b.n) {
        const uint32_t va = a.v[i], vb = b.v[j];
        if (va == vb) { ++c; ++i; ++j; }
        else if (va < vb) i = gallop(a.v, a.n, i, vb);
        else              j = gallop(b.v, b.n, j, va);
    }
    return c;
}

#if STORM_CELL_NEON
// 16x16: four vectors per side. neon8 beat neon4 on balanced pairs, so the
// question is whether the series continues or whether the rotation count
// (which grows linearly in block width) overtakes the comparisons saved.
uint64_t ss_neon16(const ListView& a, const ListView& b) {
    uint64_t c = 0;
    uint32_t i = 0, j = 0;
    while (i + 16 <= a.n && j + 16 <= b.n) {
        uint32x4_t acc = vdupq_n_u32(0);
        for (int ai = 0; ai < 16; ai += 4) {
            const uint32x4_t va = vld1q_u32(a.v + i + ai);
            for (int bi = 0; bi < 16; bi += 4) {
                uint32x4_t vb = vld1q_u32(b.v + j + bi);
                uint32x4_t m = vceqq_u32(va, vb);
                vb = vextq_u32(vb, vb, 1); m = vorrq_u32(m, vceqq_u32(va, vb));
                vb = vextq_u32(vb, vb, 1); m = vorrq_u32(m, vceqq_u32(va, vb));
                vb = vextq_u32(vb, vb, 1); m = vorrq_u32(m, vceqq_u32(va, vb));
                acc = vaddq_u32(acc, vshrq_n_u32(m, 31));
            }
        }
        c += vaddvq_u32(acc);
        const uint32_t amax = a.v[i + 15], bmax = b.v[j + 15];
        i += (amax <= bmax) ? 16 : 0;
        j += (bmax <= amax) ? 16 : 0;
    }
    while (i < a.n && j < b.n) {
        const uint32_t va = a.v[i], vb = b.v[j];
        c += (va == vb); i += (va <= vb); j += (vb <= va);
    }
    return c;
}
#endif

// Size-adaptive: merge when the sides are comparable, gallop when they are not.
// The ratio is the same kind of threshold M4 is supposed to own; it is named
// here rather than buried as a literal.
constexpr uint32_t kGallopRatio = 8;

// The ratio at which galloping overtakes a merge is a calibration constant, not
// a law. RESEARCH_PLAN.md 5.1 puts thresholds like this in the cost model (M4);
// until M4 exists they are at least sampled rather than assumed.
template <uint32_t RATIO>
uint64_t ss_adaptive_t(const ListView& a, const ListView& b) {
    if (a.n == 0 || b.n == 0) return 0;
    if (a.v[a.n - 1] < b.v[0] || b.v[b.n - 1] < a.v[0]) return 0;
    const uint32_t lo = std::min(a.n, b.n), hi = std::max(a.n, b.n);
    if (hi / lo >= RATIO) return ss_gallop_sym(a, b);
#if STORM_CELL_NEON
    return ss_neon8(a, b);
#else
    return ss_merge_bl(a, b);
#endif
}

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

#if STORM_CELL_NEON
/* Second-generation selector, built from what the sweep actually showed rather
 * than from one length ratio:
 *
 *   disjoint spans        -> 0, from two comparisons
 *   lopsided lengths      -> gallop_sym (8.53x over neon8 on long-run data)
 *   comparable lengths    -> neon8 8x8 block compare (1.76-1.77x over neon)
 *
 * gallop_sym rather than gallop because it drives from whichever side is
 * behind, which is what wins when two rows have similar cardinality but sit in
 * different parts of the universe -- the common case under a 1/i spectrum. */
uint64_t ss_adaptive2(const ListView& a, const ListView& b) {
    if (a.n == 0 || b.n == 0) return 0;
    if (a.v[a.n - 1] < b.v[0] || b.v[b.n - 1] < a.v[0]) return 0;
    const uint32_t lo = std::min(a.n, b.n), hi = std::max(a.n, b.n);
    if (hi / lo >= kGallopRatio) return ss_gallop_sym(a, b);
    return ss_neon8(a, b);
}
#endif

#if STORM_CELL_NEON
// 8x8 block compare: two vectors per side, so one advance step settles 64
// candidate pairs instead of 16. Doubles the work per branch at the cost of
// four more vceqq_u32 per step; whether that pays depends on how often the
// blocks actually overlap, which is a property of the data rather than of the
// kernel.
uint64_t ss_neon8(const ListView& a, const ListView& b) {
    uint64_t c = 0;
    uint32_t i = 0, j = 0;
    while (i + 8 <= a.n && j + 8 <= b.n) {
        const uint32x4_t a0 = vld1q_u32(a.v + i), a1 = vld1q_u32(a.v + i + 4);
        uint32x4_t b0 = vld1q_u32(b.v + j),       b1 = vld1q_u32(b.v + j + 4);
        uint32x4_t m0 = vdupq_n_u32(0), m1 = vdupq_n_u32(0);
        for (int r = 0; r < 4; ++r) {
            m0 = vorrq_u32(m0, vorrq_u32(vceqq_u32(a0, b0), vceqq_u32(a0, b1)));
            m1 = vorrq_u32(m1, vorrq_u32(vceqq_u32(a1, b0), vceqq_u32(a1, b1)));
            b0 = vextq_u32(b0, b0, 1);
            b1 = vextq_u32(b1, b1, 1);
        }
        c += vaddvq_u32(vshrq_n_u32(m0, 31)) + vaddvq_u32(vshrq_n_u32(m1, 31));
        const uint32_t amax = a.v[i + 7], bmax = b.v[j + 7];
        i += (amax <= bmax) ? 8 : 0;
        j += (bmax <= amax) ? 8 : 0;
    }
    while (i < a.n && j < b.n) {
        const uint32_t va = a.v[i], vb = b.v[j];
        c += (va == vb); i += (va <= vb); j += (vb <= va);
    }
    return c;
}

// Range pre-filter. Two sorted lists whose spans barely overlap still cost a
// full merge; clipping both to the intersection of their [min, max] ranges is
// two galloping searches and can delete most of the work. Under a 1/i spectrum
// rows land in different parts of the universe constantly.
uint64_t ss_clip(const ListView& a, const ListView& b) {
    if (a.n == 0 || b.n == 0) return 0;
    if (a.v[a.n - 1] < b.v[0] || b.v[b.n - 1] < a.v[0]) return 0;   // disjoint spans
    const uint32_t ia = gallop(a.v, a.n, 0, b.v[0]);
    const uint32_t ib = gallop(b.v, b.n, 0, a.v[0]);
    ListView ca{a.v + ia, a.n - ia};
    ListView cb{b.v + ib, b.n - ib};
    return ss_neon(ca, cb);
}
#endif

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

/* Adds the disjoint-span early out to the three-way cost comparison. Two
 * comparisons settle a pair whose list and run array do not overlap at all,
 * which under a skewed spectrum is a large fraction of them. */
uint64_t sr_adaptive2(const ListView& s, const RunView& r) {
    if (s.n == 0 || r.n == 0) return 0;
    if (s.v[s.n - 1] < r.start[0] || r.end[r.n - 1] <= s.v[0]) return 0;
    const uint64_t c_merge  = (uint64_t)s.n + r.n;
    const uint64_t c_search = (uint64_t)s.n * ilog2_up(r.n);
    const uint64_t c_runs   = (uint64_t)r.n * ilog2_up(s.n);
    if (c_runs <= c_search && c_runs <= c_merge)   return sr_search_runs(s, r);
    if (c_search < c_merge)                        return sr_search(s, r);
    return sr_merge(s, r);
}

/* The three-way comparison treats a merge step and a search step as equal cost.
 * They are not: a merge step is one compare and two conditional increments; a
 * search step is a dependent load. BIAS scales the merge's modelled cost down
 * to reflect that, and the right value is an empirical question. */
template <uint32_t BIAS>
uint64_t sr_adaptive_t(const ListView& s, const RunView& r) {
    if (s.n == 0 || r.n == 0) return 0;
    if (s.v[s.n - 1] < r.start[0] || r.end[r.n - 1] <= s.v[0]) return 0;
    const uint64_t c_merge  = ((uint64_t)s.n + r.n) / BIAS;
    const uint64_t c_search = (uint64_t)s.n * ilog2_up(r.n);
    const uint64_t c_runs   = (uint64_t)r.n * ilog2_up(s.n);
    if (c_runs <= c_search && c_runs <= c_merge)   return sr_search_runs(s, r);
    if (c_search < c_merge)                        return sr_search(s, r);
    return sr_merge(s, r);
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

#if STORM_CELL_NEON
// Test four list elements against one run at a time. A run is a half-open
// interval, so containment inside it is one vector compare per quad -- no
// branch per element, which is what sr_merge pays. Advancing the run pointer
// stays scalar because it is data dependent, but that is once per run rather
// than once per element.
uint64_t sr_neon(const ListView& s, const RunView& r) {
    uint64_t c = 0;
    uint32_t i = 0, j = 0;
    while (i < s.n && j < r.n) {
        const uint32_t lo = r.start[j], hi = r.end[j];
        while (i < s.n && s.v[i] < lo) ++i;            // skip up to this run
        const uint32x4_t vhi = vdupq_n_u32(hi);
        bool ran_out = false;
        while (i + 4 <= s.n) {
            const uint32x4_t v = vld1q_u32(s.v + i);
            const uint32_t k = vaddvq_u32(vshrq_n_u32(vcltq_u32(v, vhi), 31));
            c += k;
            i += k;
            if (k != 4) { ran_out = true; break; }     // quad straddles the run end
        }
        if (!ran_out) while (i < s.n && s.v[i] < hi) { ++c; ++i; }
        ++j;
    }
    return c;
}
#endif

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

// Disjoint-span early out, plus clipping both run arrays to the overlapping
// span before merging. Same idea as ss_clip: the cheapest overlap is the one
// proved absent from two comparisons.
uint64_t rr_clip(const RunView& a, const RunView& b) {
    if (a.n == 0 || b.n == 0) return 0;
    if (a.end[a.n - 1] <= b.start[0] || b.end[b.n - 1] <= a.start[0]) return 0;
    const uint32_t ia = gallop(a.end, a.n, 0, b.start[0] + 1);
    const uint32_t ib = gallop(b.end, b.n, 0, a.start[0] + 1);
    RunView ca{a.start + ia, a.end + ia, a.n - ia};
    RunView cb{b.start + ib, b.end + ib, b.n - ib};
    uint64_t c = 0;
    uint32_t i = 0, j = 0;
    while (i < ca.n && j < cb.n) {
        const uint32_t ea = ca.end[i], eb = cb.end[j];
        const uint32_t lo = std::max(ca.start[i], cb.start[j]);
        const uint32_t hi = std::min(ea, eb);
        c += (hi > lo) ? (hi - lo) : 0;
        i += (ea <= eb);
        j += (eb <= ea);
    }
    return c;
}

/* A vectorized R x R merge was attempted and abandoned. The overlap arithmetic
 * itself vectorizes cleanly -- max(0, min(ea,eb) - max(sa,sb)) is two vector
 * min/max and one saturating subtract, with vqsubq_u32 giving the max(0, .) for
 * free. What does not vectorize is the ADVANCE: which side steps forward
 * depends on the comparison just computed, so a SIMD block cannot know how many
 * run pairs it actually consumed. Processing four pairs positionally is only
 * correct when both arrays advance in lockstep, which is not a property the
 * data has.
 *
 * Recorded here rather than left as a gap: the reason R x R stays scalar is
 * structural, not an omission, and the same argument applies to every
 * merge-shaped cell (S x S escapes it only because the all-pairs block compare
 * sidesteps the advance question entirely). */

// Symmetric galloping over runs: skip forward on whichever side is behind,
// rather than fixing the driving side by length as rr_gallop does. Same
// motivation as ss_gallop_sym, which was the largest S x S win.
uint64_t rr_gallop_sym(const RunView& a, const RunView& b) {
    uint64_t c = 0;
    uint32_t i = 0, j = 0;
    while (i < a.n && j < b.n) {
        const uint32_t lo = std::max(a.start[i], b.start[j]);
        const uint32_t hi = std::min(a.end[i],   b.end[j]);
        if (hi > lo) { c += hi - lo; if (a.end[i] < b.end[j]) ++i; else ++j; continue; }
        if (a.end[i] <= b.start[j])      i = gallop(a.end, a.n, i, b.start[j] + 1);
        else                             j = gallop(b.end, b.n, j, a.start[i] + 1);
    }
    return c;
}

constexpr uint32_t kRrGallopRatio = 8;

template <uint32_t RATIO>
uint64_t rr_adaptive_t(const RunView& a, const RunView& b) {
    if (a.n == 0 || b.n == 0) return 0;
    if (a.end[a.n - 1] <= b.start[0] || b.end[b.n - 1] <= a.start[0]) return 0;
    const uint32_t lo = std::min(a.n, b.n), hi = std::max(a.n, b.n);
    return (hi / lo >= RATIO) ? rr_gallop(a, b) : rr_merge_bl(a, b);
}

uint64_t rr_adaptive(const RunView& a, const RunView& b) {
    const uint32_t lo = std::min(a.n, b.n), hi = std::max(a.n, b.n);
    if (lo == 0) return 0;
    return (hi / lo >= kRrGallopRatio) ? rr_gallop(a, b) : rr_merge_bl(a, b);
}

// Adaptive with the disjoint-span early out in front of it.
uint64_t rr_adaptive2(const RunView& a, const RunView& b) {
    if (a.n == 0 || b.n == 0) return 0;
    if (a.end[a.n - 1] <= b.start[0] || b.end[b.n - 1] <= a.start[0]) return 0;
    const uint32_t lo = std::min(a.n, b.n), hi = std::max(a.n, b.n);
    return (hi / lo >= kRrGallopRatio) ? rr_gallop(a, b) : rr_merge_bl(a, b);
}

// ---------------------------------------------------------------------------

const Variant<fn_ss> kSS[] = {
    {"merge",     ss_merge,     "reference: branchy sorted merge"},
    {"merge_bl",  ss_merge_bl,  "branchless merge"},
    {"gallop",    ss_gallop,    "exponential + binary search from the shorter side"},
    {"gallop_sym",ss_gallop_sym,"gallop whichever side is behind, both directions"},
#if STORM_CELL_NEON
    {"neon8",     ss_neon8,     "8x8 block compare -- 64 candidate pairs per step"},
    {"clip",      ss_clip,      "clip both lists to their overlapping span first"},
    {"neon16",    ss_neon16,    "16x16 block compare -- does the block-width series continue?"},
    {"adapt_r2",  ss_adaptive_t<2>,  "adaptive2 with the gallop ratio at 2"},
    {"adapt_r6",  ss_adaptive_t<6>,  "adaptive2 with the gallop ratio at 6"},
    {"adapt_r48", ss_adaptive_t<48>, "adaptive2 with the gallop ratio at 48"},
    {"adapt_r3",  ss_adaptive_t<3>,  "adaptive2 with the gallop ratio at 3"},
    {"adapt_r24", ss_adaptive_t<24>, "adaptive2 with the gallop ratio at 24"},
    {"adaptive2", ss_adaptive2, "disjoint -> 0, lopsided -> gallop_sym, else neon8"},
#endif
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
#if STORM_CELL_NEON
    {"neon",      sr_neon,      "4 list elements tested against a run per step"},
#endif
#if STORM_CELL_NEON
    {"neon",      sr_neon,      "4 list elements tested against a run per step"},
#endif
    {"adaptive",  sr_adaptive,  "three-way cost comparison from the two sizes"},
    {"adaptive2", sr_adaptive2, "three-way cost comparison plus a disjoint-span early out"},
    {"adapt_b1",  sr_adaptive_t<1>,  "cost comparison unbiased"},
    {"adapt_b4",  sr_adaptive_t<4>,  "cost comparison biased 4x toward the merge"},
    {"adapt_b16", sr_adaptive_t<16>, "cost comparison biased 16x toward the merge"},
    {"adapt_b2",  sr_adaptive_t<2>,  "cost comparison biased 2x toward the merge"},
    {"adapt_b8",  sr_adaptive_t<8>,  "cost comparison biased 8x toward the merge"},
};

const Variant<fn_rr> kRR[] = {
    {"merge",     rr_merge,     "reference: overlap merge"},
    {"merge_bl",  rr_merge_bl,  "branchless merge"},
    {"gallop",    rr_gallop,    "gallop from the shorter run array"},
    {"adaptive",  rr_adaptive,  "merge or gallop on the run-count ratio"},
    {"clip",      rr_clip,      "disjoint-span early out, then clip both sides"},
    {"adaptive2", rr_adaptive2, "disjoint-span early out in front of the ratio choice"},
    {"gallop_sym",rr_gallop_sym,"gallop whichever run array is behind"},
    {"adapt_r2",  rr_adaptive_t<2>,  "adaptive2 with the gallop ratio at 2"},
    {"adapt_r6",  rr_adaptive_t<6>,  "adaptive2 with the gallop ratio at 6"},
    {"adapt_r48", rr_adaptive_t<48>, "adaptive2 with the gallop ratio at 48"},
    {"adapt_r3",  rr_adaptive_t<3>,  "adaptive2 with the gallop ratio at 3"},
    {"adapt_r24", rr_adaptive_t<24>, "adaptive2 with the gallop ratio at 24"},
};

} // namespace

VariantList<fn_ss> cell_ss() { return {kSS, sizeof(kSS) / sizeof(kSS[0])}; }
VariantList<fn_sr> cell_sr() { return {kSR, sizeof(kSR) / sizeof(kSR[0])}; }
VariantList<fn_rr> cell_rr() { return {kRR, sizeof(kRR) / sizeof(kRR[0])}; }

} // namespace storm
