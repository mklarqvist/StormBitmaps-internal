/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist
 * Licensed under the Apache License, Version 2.0.
 *
 * The WAH-fill cells: B x W, S x W, R x W, W x W.
 *
 * W is EWAH-64 (see storm_repr.h for the layout and for why EWAH rather than
 * classic 31-bit-literal WAH). The property that matters for every cell here:
 * a ZERO fill contributes nothing to an intersection and can be skipped in O(1)
 * regardless of how many words it covers. That is the same mechanism as the
 * rank index in B x R, arrived at from the representation side instead of the
 * index side, and it is why W is a credible alternative to R for data that is
 * blocky rather than run-structured.
 *
 * All four cells are P1/P2 in RESEARCH_PLAN.md 1. They are built now because
 * the selection model cannot choose W without a measured cost for it, and
 * because B x W is the direct test of "does the rank index also accelerate
 * fills?" (RESEARCH_PLAN.md 4.5, last bullet).
 */
#include "kernels/storm_cells.h"
#include "kernels/storm_simd.h"

#include <algorithm>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#  include <arm_neon.h>
#  define STORM_CELL_NEON 1
#else
#  define STORM_CELL_NEON 0
#endif

namespace storm {
namespace {

// The shared, multi-accumulator primitives from storm_simd.h. The private
// single-accumulator versions these replaced lost to the auto-vectorized scalar
// reference on the long-fill corpus; see storm_simd.h for the measurement.
static inline uint64_t popcnt_words(const uint64_t* w, uint32_t n) {
    return simd_popcnt(w, n);
}
static inline uint64_t and_popcnt_words(const uint64_t* a, const uint64_t* b, uint32_t n) {
    return simd_and_popcnt(a, b, n);
}

// ---------------------------------------------------------------------------
// EWAH cursor.
//
// Presents the stream as a sequence of homogeneous SEGMENTS -- a run of fill
// words, or a run of literal words -- so every cell below is a segment merge and
// none of them has to know the marker encoding. `consume(k)` takes k words off
// the front of the current segment, which is what a merge against another stream
// needs: two cursors always advance by the min of their segment lengths.
// ---------------------------------------------------------------------------
struct Cursor {
    const uint64_t* buf;
    uint32_t n, i;
    uint32_t fill_left, lit_left;
    bool     fill_val;

    explicit Cursor(const EwahView& w)
        : buf(w.buf), n(w.n), i(0), fill_left(0), lit_left(0), fill_val(false) {
        load();
    }
    void load() {
        while (i < n) {
            const uint64_t m = buf[i++];
            fill_val  = ewah_fill_bit(m);
            fill_left = (uint32_t)ewah_fill_len(m);
            lit_left  = (uint32_t)ewah_lit_len(m);
            if (fill_left || lit_left) return;
        }
        fill_left = lit_left = 0;
    }
    bool     done()    const { return fill_left == 0 && lit_left == 0; }
    bool     in_fill() const { return fill_left != 0; }
    uint32_t seg()     const { return fill_left ? fill_left : lit_left; }
    // Valid only when !in_fill(): `i` still points at the first literal of the
    // current marker because load() advanced past the marker word itself.
    const uint64_t* lit() const { return buf + i; }

    void consume(uint32_t k) {
        if (fill_left) {
            fill_left -= k;
            if (!fill_left && !lit_left) load();
        } else {
            lit_left -= k;
            i        += k;
            if (!lit_left) load();
        }
    }
};

// ===========================================================================
// B x W
// ===========================================================================

uint64_t bw_scalar(const BitmapView& b, const EwahView& w) {
    uint64_t c = 0;
    uint32_t pos = 0;                 // word offset into the bitmap
    Cursor cw(w);
    while (!cw.done() && pos < b.nw) {
        uint32_t k = cw.seg();
        if (pos + k > b.nw) k = b.nw - pos;
        if (cw.in_fill()) {
            // A zero fill is free: k words skipped with no memory touched.
            if (cw.fill_val) for (uint32_t t = 0; t < k; ++t) c += STORM_POPCOUNT(b.w[pos + t]);
        } else {
            for (uint32_t t = 0; t < k; ++t) c += STORM_POPCOUNT(b.w[pos + t] & cw.lit()[t]);
        }
        pos += k;
        cw.consume(k);
    }
    return c;
}

uint64_t bw_neon(const BitmapView& b, const EwahView& w) {
    uint64_t c = 0;
    uint32_t pos = 0;
    Cursor cw(w);
    while (!cw.done() && pos < b.nw) {
        uint32_t k = cw.seg();
        if (pos + k > b.nw) k = b.nw - pos;
        if (cw.in_fill()) {
            if (cw.fill_val) c += popcnt_words(b.w + pos, k);
        } else {
            c += and_popcnt_words(b.w + pos, cw.lit(), k);
        }
        pos += k;
        cw.consume(k);
    }
    return c;
}

// The direct test of RESEARCH_PLAN.md 4.5's last question: does the rank index
// also accelerate B x W? A one-fill of k words is exactly a run, so it should
// collapse to a single rank difference and become independent of k. Literals
// still cost a word each — they are irreducible — so the win here is bounded by
// how much of the stream is fills, which is the honest limit and worth showing.
uint64_t bw_rank(const BitmapView& b, const EwahView& w) {
    if (b.rank == nullptr) return bw_neon(b, w);
    uint64_t c = 0;
    uint32_t pos = 0;
    Cursor cw(w);
    while (!cw.done() && pos < b.nw) {
        uint32_t k = cw.seg();
        if (pos + k > b.nw) k = b.nw - pos;
        if (cw.in_fill()) {
            if (cw.fill_val)
                c += rank_at(b, (pos + k) * 64) - rank_at(b, pos * 64);
        } else {
            c += and_popcnt_words(b.w + pos, cw.lit(), k);
        }
        pos += k;
        cw.consume(k);
    }
    return c;
}

// ===========================================================================
// W x W
// ===========================================================================

uint64_t ww_scalar(const EwahView& a, const EwahView& b) {
    uint64_t c = 0;
    Cursor ca(a), cb(b);
    while (!ca.done() && !cb.done()) {
        const uint32_t k = std::min(ca.seg(), cb.seg());
        if (ca.in_fill() && cb.in_fill()) {
            if (ca.fill_val && cb.fill_val) c += uint64_t(k) * 64;
        } else if (ca.in_fill()) {
            if (ca.fill_val) for (uint32_t t = 0; t < k; ++t) c += STORM_POPCOUNT(cb.lit()[t]);
        } else if (cb.in_fill()) {
            if (cb.fill_val) for (uint32_t t = 0; t < k; ++t) c += STORM_POPCOUNT(ca.lit()[t]);
        } else {
            for (uint32_t t = 0; t < k; ++t) c += STORM_POPCOUNT(ca.lit()[t] & cb.lit()[t]);
        }
        ca.consume(k);
        cb.consume(k);
    }
    return c;
}

uint64_t ww_neon(const EwahView& a, const EwahView& b) {
    uint64_t c = 0;
    Cursor ca(a), cb(b);
    while (!ca.done() && !cb.done()) {
        const uint32_t k = std::min(ca.seg(), cb.seg());
        if (ca.in_fill() && cb.in_fill()) {
            if (ca.fill_val && cb.fill_val) c += uint64_t(k) * 64;
        } else if (ca.in_fill()) {
            if (ca.fill_val) c += popcnt_words(cb.lit(), k);
        } else if (cb.in_fill()) {
            if (cb.fill_val) c += popcnt_words(ca.lit(), k);
        } else {
            c += and_popcnt_words(ca.lit(), cb.lit(), k);
        }
        ca.consume(k);
        cb.consume(k);
    }
    return c;
}

// A zero fill on either side annihilates the segment, so the whole overlap can
// be skipped without inspecting the other stream at all. ww_neon already does
// no work in that case, but it still advances one min-segment at a time; this
// variant lets a long zero fill swallow many short segments of the other side
// in one step, which is the regime where W is supposed to win.
uint64_t ww_skip(const EwahView& a, const EwahView& b) {
    uint64_t c = 0;
    Cursor ca(a), cb(b);
    while (!ca.done() && !cb.done()) {
        if (ca.in_fill() && !ca.fill_val) {            // A is zero: skip A's whole fill
            uint32_t k = ca.fill_left;
            while (k && !cb.done()) {
                const uint32_t t = std::min(k, cb.seg());
                cb.consume(t);
                k -= t;
            }
            ca.consume(ca.fill_left - k);
            continue;
        }
        if (cb.in_fill() && !cb.fill_val) {
            uint32_t k = cb.fill_left;
            while (k && !ca.done()) {
                const uint32_t t = std::min(k, ca.seg());
                ca.consume(t);
                k -= t;
            }
            cb.consume(cb.fill_left - k);
            continue;
        }
        const uint32_t k = std::min(ca.seg(), cb.seg());
        if (ca.in_fill() && cb.in_fill())      c += uint64_t(k) * 64;   // both one-fills
        else if (ca.in_fill())                 c += popcnt_words(cb.lit(), k);
        else if (cb.in_fill())                 c += popcnt_words(ca.lit(), k);
        else                                   c += and_popcnt_words(ca.lit(), cb.lit(), k);
        ca.consume(k);
        cb.consume(k);
    }
    return c;
}

// ===========================================================================
// S x W
// ===========================================================================

// Merge the sorted list against the segment stream. Each list element lands in
// exactly one segment; because the list is sorted the cursor only ever moves
// forward, so this is Theta(|S| + segments).
uint64_t sw_merge(const ListView& s, const EwahView& w) {
    uint64_t c = 0;
    Cursor cw(w);
    uint32_t pos = 0;                 // word offset of the current segment start
    for (uint32_t i = 0; i < s.n; ++i) {
        const uint32_t wi = s.v[i] >> 6;
        while (!cw.done() && wi >= pos + cw.seg()) {
            pos += cw.seg();
            cw.consume(cw.seg());
        }
        if (cw.done()) break;
        if (cw.in_fill()) c += cw.fill_val ? 1 : 0;
        else              c += (cw.lit()[wi - pos] >> (s.v[i] & 63)) & 1u;
    }
    return c;
}

// Same merge, but same-word list elements are folded into one mask first, as in
// the B x S D2 design. Clustered data hits this constantly.
uint64_t sw_collapse(const ListView& s, const EwahView& w) {
    uint64_t c = 0;
    Cursor cw(w);
    uint32_t pos = 0;
    uint32_t i = 0;
    while (i < s.n) {
        const uint32_t wi = s.v[i] >> 6;
        uint64_t mask = 0;
        uint32_t cnt = 0;
        do { mask |= uint64_t(1) << (s.v[i] & 63); ++i; ++cnt; }
        while (i < s.n && (s.v[i] >> 6) == wi);

        while (!cw.done() && wi >= pos + cw.seg()) {
            pos += cw.seg();
            cw.consume(cw.seg());
        }
        if (cw.done()) break;
        if (cw.in_fill()) c += cw.fill_val ? cnt : 0;
        else              c += STORM_POPCOUNT(cw.lit()[wi - pos] & mask);
    }
    return c;
}

// ===========================================================================
// R x W
// ===========================================================================

// Set bits of a word array in [lo, hi), where word 0 of `w` covers bits
// [base * 64, base * 64 + 64).
static inline uint64_t range_in_words(const uint64_t* w, uint32_t base,
                                      uint32_t lo, uint32_t hi) {
    if (lo >= hi) return 0;
    const uint32_t wlo = (lo >> 6) - base, whi = ((hi - 1) >> 6) - base;
    const uint64_t mlo = ~uint64_t(0) << (lo & 63);
    const uint64_t mhi = (hi & 63) ? ((uint64_t(1) << (hi & 63)) - 1) : ~uint64_t(0);
    if (wlo == whi) return STORM_POPCOUNT(w[wlo] & mlo & mhi);
    uint64_t c = STORM_POPCOUNT(w[wlo] & mlo) + STORM_POPCOUNT(w[whi] & mhi);
    if (whi > wlo + 1) c += popcnt_words(w + wlo + 1, whi - wlo - 1);
    return c;
}

// For each run, count W's set bits inside it. The master cursor only retires
// segments that end before the current run starts, so it advances monotonically
// across the whole call; a run that spans several segments walks them with a
// local copy and leaves the master alone, which keeps the total at
// Theta(r + segments) rather than Theta(r * segments).
uint64_t rw_merge(const RunView& r, const EwahView& w) {
    uint64_t c = 0;
    Cursor cw(w);
    uint32_t pos = 0;
    for (uint32_t i = 0; i < r.n; ++i) {
        const uint32_t lo = r.start[i], hi = r.end[i];
        while (!cw.done() && (uint64_t)(pos + cw.seg()) * 64 <= lo) {
            pos += cw.seg();
            cw.consume(cw.seg());
        }
        if (cw.done()) break;

        Cursor lc = cw;
        uint32_t lp = pos;
        while (!lc.done() && (uint64_t)lp * 64 < hi) {
            const uint32_t k    = lc.seg();
            const uint32_t sbit = lp * 64;
            const uint32_t ebit = (lp + k) * 64;
            const uint32_t a = std::max(lo, sbit), bnd = std::min(hi, ebit);
            if (bnd > a) {
                if (lc.in_fill()) { if (lc.fill_val) c += bnd - a; }
                else              c += range_in_words(lc.lit(), lp, a, bnd);
            }
            lp += k;
            lc.consume(k);
        }
    }
    return c;
}

// ---------------------------------------------------------------------------

const Variant<fn_bw> kBW[] = {
    {"scalar",   bw_scalar,   "reference: segment walk, word at a time"},
    {"neon",     bw_neon,     "NEON literal and fill bodies"},
    {"rank",     bw_rank,     "one-fills collapse to a rank difference", true},
};

const Variant<fn_sw> kSW[] = {
    {"merge",    sw_merge,    "reference: list against the segment stream"},
    {"collapse", sw_collapse, "same-word list elements folded into one mask"},
};

const Variant<fn_rw> kRW[] = {
    {"merge",    rw_merge,    "reference: runs against the segment stream"},
};

const Variant<fn_ww> kWW[] = {
    {"scalar",   ww_scalar,   "reference: segment merge, word at a time"},
    {"neon",     ww_neon,     "NEON literal bodies"},
    {"skip",     ww_skip,     "a zero fill swallows the other side's segments whole"},
};

} // namespace

VariantList<fn_bw> cell_bw() { return {kBW, sizeof(kBW) / sizeof(kBW[0])}; }
VariantList<fn_sw> cell_sw() { return {kSW, sizeof(kSW) / sizeof(kSW[0])}; }
VariantList<fn_rw> cell_rw() { return {kRW, sizeof(kRW) / sizeof(kRW[0])}; }
VariantList<fn_ww> cell_ww() { return {kWW, sizeof(kWW) / sizeof(kWW[0])}; }

} // namespace storm
