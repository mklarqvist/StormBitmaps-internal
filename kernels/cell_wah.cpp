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
#include <cassert>

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
        // k must not exceed the current segment. Unchecked, `fill_left -= k`
        // wraps uint32_t to ~4e9 and the merge loop runs away silently instead
        // of failing; with ten cells sharing this cursor that is the highest-
        // leverage place in the file for a latent bug to hide.
        assert(k <= (fill_left ? fill_left : lit_left));
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

// Fill-length adaptive. bw_rank collapses EVERY one-fill to a rank difference,
// but B x R measured the rank/direct crossover at ~4 words of run -- and a fill
// is a run. Below that the two rank probes cost more than just counting the
// words. Same threshold, same reason.
constexpr uint32_t kBwRankMinWords = 4;

template <uint32_t MINW>
uint64_t bw_hybrid_t(const BitmapView& b, const EwahView& w) {
    if (b.rank == nullptr) return bw_neon(b, w);
    uint64_t c = 0;
    uint32_t pos = 0;
    Cursor cw(w);
    while (!cw.done() && pos < b.nw) {
        uint32_t k = cw.seg();
        if (pos + k > b.nw) k = b.nw - pos;
        if (cw.in_fill()) {
            if (cw.fill_val) {
                if (k >= MINW) c += rank_at(b, (pos + k) * 64) - rank_at(b, pos * 64);
                else           c += popcnt_words(b.w + pos, k);
            }
        } else {
            c += and_popcnt_words(b.w + pos, cw.lit(), k);
        }
        pos += k;
        cw.consume(k);
    }
    return c;
}

uint64_t bw_hybrid(const BitmapView& b, const EwahView& w) {
    if (b.rank == nullptr) return bw_neon(b, w);
    uint64_t c = 0;
    uint32_t pos = 0;
    Cursor cw(w);
    while (!cw.done() && pos < b.nw) {
        uint32_t k = cw.seg();
        if (pos + k > b.nw) k = b.nw - pos;
        if (cw.in_fill()) {
            if (cw.fill_val) {
                if (k >= kBwRankMinWords)
                    c += rank_at(b, (pos + k) * 64) - rank_at(b, pos * 64);
                else
                    c += popcnt_words(b.w + pos, k);
            }
        } else {
            c += and_popcnt_words(b.w + pos, cw.lit(), k);
        }
        pos += k;
        cw.consume(k);
    }
    return c;
}

/* Bulk zero-fill skipping for B x W. bw_hybrid still advances one segment at a
 * time; this lets a long zero fill retire without the per-segment bookkeeping,
 * and uses the rank difference for one-fills above the measured crossover.
 * The combination of both skips is what the density sweep says should win at
 * the sparse end. */
uint64_t bw_skip(const BitmapView& b, const EwahView& w) {
    uint64_t c = 0;
    uint32_t pos = 0;
    Cursor cw(w);
    const bool have_rank = (b.rank != nullptr);
    while (!cw.done() && pos < b.nw) {
        uint32_t k = cw.seg();
        if (pos + k > b.nw) k = b.nw - pos;
        if (cw.in_fill()) {
            if (!cw.fill_val) {            // zero fill: nothing to do at any length
                pos += k;
                cw.consume(cw.seg());
                continue;
            }
            if (have_rank && k >= kBwRankMinWords)
                c += rank_at(b, (pos + k) * 64) - rank_at(b, pos * 64);
            else
                c += popcnt_words(b.w + pos, k);
        } else {
            c += and_popcnt_words(b.w + pos, cw.lit(), k);
        }
        pos += k;
        cw.consume(k);
    }
    return c;
}

// Zone-map planning for B x W. A literal segment whose bins are empty on the
// bitmap side needs no AND at all; the summary settles it from one bit per bin.
// Same prediction as br_occ: the fill path is already O(1) via rank, so the
// zone map can only remove work from the LITERAL path, which bounds the win by
// the literal fraction of the stream.
uint64_t bw_occ(const BitmapView& b, const EwahView& w) {
    if (b.occ == nullptr) return bw_skip(b, w);
    const uint32_t BW = BitmapView::OCC_BIN_WORDS;
    uint64_t c = 0;
    uint32_t pos = 0;
    Cursor cw(w);
    const bool have_rank = (b.rank != nullptr);
    while (!cw.done() && pos < b.nw) {
        uint32_t k = cw.seg();
        if (pos + k > b.nw) k = b.nw - pos;
        if (cw.in_fill()) {
            if (!cw.fill_val) { pos += k; cw.consume(cw.seg()); continue; }
            if (have_rank && k >= kBwRankMinWords)
                c += rank_at(b, (pos + k) * 64) - rank_at(b, pos * 64);
            else
                c += popcnt_words(b.w + pos, k);
        } else {
            // Literals: process bin by bin, skipping bins the summary says are
            // empty on the bitmap side.
            uint32_t t = 0;
            while (t < k) {
                const uint32_t bin  = (pos + t) / BW;
                const uint32_t stop = std::min(k, (bin + 1) * BW - pos);
                const uint32_t ow   = bin >> 6;
                if (ow < b.n_occ && ((b.occ[ow] >> (bin & 63)) & 1u))
                    c += and_popcnt_words(b.w + pos + t, cw.lit() + t, stop - t);
                t = stop;
            }
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

// A zero fill contributes nothing, so every list element inside it can be
// skipped WHOLESALE rather than tested one at a time. sw_merge already does no
// arithmetic for those elements, but it still walks them; this variant advances
// the list index past the fill in one step.
//
// The work-reduction counterpart to sw_collapse, which reduces the LITERAL-side
// cost. Under a skewed spectrum the dense side is mostly zero fills, so this is
// where the elements actually go.
uint64_t sw_skip(const ListView& s, const EwahView& w) {
    uint64_t c = 0;
    Cursor cw(w);
    uint32_t pos = 0;                 // word offset of the current segment start
    uint32_t i = 0;
    while (i < s.n && !cw.done()) {
        const uint32_t wi = s.v[i] >> 6;
        if (wi >= pos + cw.seg()) {   // element is past this segment
            pos += cw.seg();
            cw.consume(cw.seg());
            continue;
        }
        if (cw.in_fill() && !cw.fill_val) {
            // Skip every element that lands inside this zero fill.
            const uint64_t limit = (uint64_t)(pos + cw.seg()) * 64;
            while (i < s.n && s.v[i] < limit) ++i;
            continue;
        }
        if (cw.in_fill()) {           // one-fill: every element inside counts
            const uint64_t limit = (uint64_t)(pos + cw.seg()) * 64;
            while (i < s.n && s.v[i] < limit) { ++c; ++i; }
            continue;
        }
        c += (cw.lit()[wi - pos] >> (s.v[i] & 63)) & 1u;
        ++i;
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

/* Two-cursor merge over runs and segments.
 *
 * rw_merge walks the segment stream with a LOCAL copy for each run, which keeps
 * it correct when a run spans many segments but re-walks those segments for
 * every run that touches them. This form advances a single pair of cursors and
 * retires whichever side ends first, so each run and each segment is visited
 * exactly once: Theta(r + segments) with no re-walking, and a zero fill costs
 * one comparison no matter how many words it covers.
 */
uint64_t rw_merge2(const RunView& r, const EwahView& w) {
    uint64_t c = 0;
    Cursor cw(w);
    uint32_t pos = 0, i = 0;
    while (i < r.n && !cw.done()) {
        const uint64_t slo = (uint64_t)pos * 64;
        const uint64_t shi = (uint64_t)(pos + cw.seg()) * 64;
        if (r.end[i] <= slo) { ++i; continue; }                       // run is behind
        if (r.start[i] >= shi) { pos += cw.seg(); cw.consume(cw.seg()); continue; }

        const uint32_t a = (uint32_t)std::max<uint64_t>(r.start[i], slo);
        const uint32_t b = (uint32_t)std::min<uint64_t>(r.end[i],   shi);
        if (cw.in_fill()) { if (cw.fill_val) c += b - a; }
        else              c += range_in_words(cw.lit(), pos, a, b);

        if ((uint64_t)r.end[i] <= shi) ++i;
        else { pos += cw.seg(); cw.consume(cw.seg()); }
    }
    return c;
}

/* Same merge, but a ZERO fill retires runs in bulk instead of one at a time.
 * Under a skewed spectrum most of the stream is zero fills, so most runs are
 * settled by this branch and never reach the counting path at all. */
uint64_t rw_skip(const RunView& r, const EwahView& w) {
    uint64_t c = 0;
    Cursor cw(w);
    uint32_t pos = 0, i = 0;
    while (i < r.n && !cw.done()) {
        const uint64_t slo = (uint64_t)pos * 64;
        const uint64_t shi = (uint64_t)(pos + cw.seg()) * 64;
        if (r.end[i] <= slo) { ++i; continue; }
        if (r.start[i] >= shi) { pos += cw.seg(); cw.consume(cw.seg()); continue; }

        if (cw.in_fill() && !cw.fill_val) {          // zero fill: skip every run in it
            while (i < r.n && (uint64_t)r.end[i] <= shi) ++i;
            pos += cw.seg();
            cw.consume(cw.seg());
            continue;
        }
        const uint32_t a = (uint32_t)std::max<uint64_t>(r.start[i], slo);
        const uint32_t b = (uint32_t)std::min<uint64_t>(r.end[i],   shi);
        if (cw.in_fill()) c += b - a;                // one fill
        else              c += range_in_words(cw.lit(), pos, a, b);

        if ((uint64_t)r.end[i] <= shi) ++i;
        else { pos += cw.seg(); cw.consume(cw.seg()); }
    }
    return c;
}

const Variant<fn_bw> kBW[] = {
    {"scalar",   bw_scalar,   "reference: segment walk, word at a time"},
    {"neon",     bw_neon,     "NEON literal and fill bodies"},
    {"rank",     bw_rank,     "one-fills collapse to a rank difference", true},
    {"hybrid",   bw_hybrid,   "rank for fills >= 4 words, direct below",  true},
    {"skip",     bw_skip,     "zero fills retire outright, one-fills via rank", true},
    {"occ",      bw_occ,      "zone map skips literal bins empty on the bitmap side", true},
};

/* Binary-search the list past each segment instead of walking it.
 *
 * sw_skip advances the list index linearly through a fill; when the fill is
 * long and the list is dense inside it that is still Theta(elements). Galloping
 * to the fill's end is Theta(log), and for a ZERO fill the elements skipped
 * never needed to be looked at individually at all. */
uint64_t sw_search(const ListView& s, const EwahView& w) {
    uint64_t c = 0;
    Cursor cw(w);
    uint32_t pos = 0, i = 0;
    while (i < s.n && !cw.done()) {
        const uint64_t shi = (uint64_t)(pos + cw.seg()) * 64;
        if ((uint64_t)s.v[i] >= shi) { pos += cw.seg(); cw.consume(cw.seg()); continue; }
        if (cw.in_fill()) {
            // Every element below shi is settled by the fill value alone.
            const uint32_t key = (shi > UINT32_MAX) ? UINT32_MAX : (uint32_t)shi;
            uint32_t j = i, step = 1;
            while (j + step < s.n && s.v[j + step] < key) { j += step; step <<= 1; }
            uint32_t hi = std::min<uint32_t>(j + step, s.n);
            while (j < hi) {
                const uint32_t mid = j + ((hi - j) >> 1);
                if (s.v[mid] < key) j = mid + 1; else hi = mid;
            }
            if (cw.fill_val) c += j - i;
            i = j;
            continue;
        }
        c += (cw.lit()[(s.v[i] >> 6) - pos] >> (s.v[i] & 63)) & 1u;
        ++i;
    }
    return c;
}

/* sw_skip walks the list through a fill; sw_search gallops past it. The sweep
 * put 13.7x between them on long-fill data and had sw_skip ahead on short-fill
 * data, so neither dominates. The crossover is a fill length: galloping costs
 * ~log(elements) and walking costs the elements themselves, so gallop once a
 * fill is long enough to contain many of them. Expressed in WORDS of fill,
 * which is what the cursor knows without touching the list. */
constexpr uint32_t kSwSearchMinFill = 8;

template <uint32_t MINFILL>
uint64_t sw_adaptive_t(const ListView& s, const EwahView& w) {
    uint64_t c = 0;
    Cursor cw(w);
    uint32_t pos = 0, i = 0;
    while (i < s.n && !cw.done()) {
        const uint64_t shi = (uint64_t)(pos + cw.seg()) * 64;
        if ((uint64_t)s.v[i] >= shi) { pos += cw.seg(); cw.consume(cw.seg()); continue; }
        if (cw.in_fill()) {
            const uint32_t key = (shi > UINT32_MAX) ? UINT32_MAX : (uint32_t)shi;
            uint32_t j = i;
            if (cw.seg() >= MINFILL) {
                uint32_t step = 1;
                while (j + step < s.n && s.v[j + step] < key) { j += step; step <<= 1; }
                uint32_t hi = std::min<uint32_t>(j + step, s.n);
                while (j < hi) {
                    const uint32_t mid = j + ((hi - j) >> 1);
                    if (s.v[mid] < key) j = mid + 1; else hi = mid;
                }
            } else {
                while (j < s.n && s.v[j] < key) ++j;
            }
            if (cw.fill_val) c += j - i;
            i = j;
            continue;
        }
        c += (cw.lit()[(s.v[i] >> 6) - pos] >> (s.v[i] & 63)) & 1u;
        ++i;
    }
    return c;
}

uint64_t sw_adaptive(const ListView& s, const EwahView& w) {
    uint64_t c = 0;
    Cursor cw(w);
    uint32_t pos = 0, i = 0;
    while (i < s.n && !cw.done()) {
        const uint64_t shi = (uint64_t)(pos + cw.seg()) * 64;
        if ((uint64_t)s.v[i] >= shi) { pos += cw.seg(); cw.consume(cw.seg()); continue; }
        if (cw.in_fill()) {
            const uint32_t key = (shi > UINT32_MAX) ? UINT32_MAX : (uint32_t)shi;
            uint32_t j = i;
            if (cw.seg() >= kSwSearchMinFill) {          // long fill: gallop past it
                uint32_t step = 1;
                while (j + step < s.n && s.v[j + step] < key) { j += step; step <<= 1; }
                uint32_t hi = std::min<uint32_t>(j + step, s.n);
                while (j < hi) {
                    const uint32_t mid = j + ((hi - j) >> 1);
                    if (s.v[mid] < key) j = mid + 1; else hi = mid;
                }
            } else {                                      // short fill: just walk
                while (j < s.n && s.v[j] < key) ++j;
            }
            if (cw.fill_val) c += j - i;
            i = j;
            continue;
        }
        c += (cw.lit()[(s.v[i] >> 6) - pos] >> (s.v[i] & 63)) & 1u;
        ++i;
    }
    return c;
}

const Variant<fn_sw> kSW[] = {
    {"merge",    sw_merge,    "reference: list against the segment stream"},
    {"collapse", sw_collapse, "same-word list elements folded into one mask"},
    {"skip",     sw_skip,     "a fill settles every list element inside it in one step"},
    {"search",   sw_search,   "gallop the list past a fill instead of walking it"},
    {"adaptive", sw_adaptive, "gallop past long fills, walk short ones"},
    {"adapt_f1", sw_adaptive_t<1>,   "gallop past every fill"},
    {"adapt_f64",sw_adaptive_t<64>, "gallop only past fills of 64 words or more"},
};

/* rw_merge2 and rw_skip traded wins across the corpora with margins under 10%,
 * which is what you expect when the only difference is a bulk-retire branch
 * that fires often on some data and never on others. Taking the skip form
 * unconditionally is the right call -- the branch is one comparison and it is
 * predictable in both directions -- but the adaptive is measured rather than
 * assumed so the claim is checkable. */
uint64_t rw_adaptive(const RunView& r, const EwahView& w) {
    if (r.n == 0) return 0;
    return rw_skip(r, w);
}

/* Branchless advance in the two-cursor merge. Every step of rw_merge2 ends in a
 * data-dependent branch choosing which side to retire; on interleaved runs and
 * segments that mispredicts about half the time. */
uint64_t rw_merge_bl(const RunView& r, const EwahView& w) {
    uint64_t c = 0;
    Cursor cw(w);
    uint32_t pos = 0, i = 0;
    while (i < r.n && !cw.done()) {
        const uint64_t slo = (uint64_t)pos * 64;
        const uint64_t shi = (uint64_t)(pos + cw.seg()) * 64;
        if (r.end[i] <= slo) { ++i; continue; }
        if (r.start[i] >= shi) { pos += cw.seg(); cw.consume(cw.seg()); continue; }
        const uint32_t a = (uint32_t)std::max<uint64_t>(r.start[i], slo);
        const uint32_t b = (uint32_t)std::min<uint64_t>(r.end[i],   shi);
        const bool fill = cw.in_fill();
        c += fill ? (cw.fill_val ? (uint64_t)(b - a) : 0)
                  : range_in_words(cw.lit(), pos, a, b);
        const bool run_first = (uint64_t)r.end[i] <= shi;
        i += run_first;
        if (!run_first) { pos += cw.seg(); cw.consume(cw.seg()); }
    }
    return c;
}

/* Both-sided bulk skip: a zero fill retires runs wholesale (as rw_skip does)
 * AND a one-fill absorbs whole runs with a single add rather than a range
 * count, since every bit of the run is set on the W side. */
uint64_t rw_skip2(const RunView& r, const EwahView& w) {
    uint64_t c = 0;
    Cursor cw(w);
    uint32_t pos = 0, i = 0;
    while (i < r.n && !cw.done()) {
        const uint64_t slo = (uint64_t)pos * 64;
        const uint64_t shi = (uint64_t)(pos + cw.seg()) * 64;
        if (r.end[i] <= slo) { ++i; continue; }
        if (r.start[i] >= shi) { pos += cw.seg(); cw.consume(cw.seg()); continue; }
        if (cw.in_fill()) {
            const bool one = cw.fill_val;
            while (i < r.n && (uint64_t)r.end[i] <= shi) {
                if (one) c += r.end[i] - (uint32_t)std::max<uint64_t>(r.start[i], slo);
                ++i;
            }
            if (i < r.n && (uint64_t)r.start[i] < shi && one)
                c += (uint32_t)(shi - std::max<uint64_t>(r.start[i], slo));
            pos += cw.seg();
            cw.consume(cw.seg());
            continue;
        }
        const uint32_t a = (uint32_t)std::max<uint64_t>(r.start[i], slo);
        const uint32_t b = (uint32_t)std::min<uint64_t>(r.end[i],   shi);
        c += range_in_words(cw.lit(), pos, a, b);
        if ((uint64_t)r.end[i] <= shi) ++i;
        else { pos += cw.seg(); cw.consume(cw.seg()); }
    }
    return c;
}

/* Bulk-skip only above a minimum fill length. Retiring runs wholesale inside a
 * zero fill costs a loop that is pure overhead when the fill covers one or two
 * words; MINFILL is where it starts paying. Same calibration question the other
 * eight cells were asked. */
template <uint32_t MINFILL>
uint64_t rw_skip_t(const RunView& r, const EwahView& w) {
    uint64_t c = 0;
    Cursor cw(w);
    uint32_t pos = 0, i = 0;
    while (i < r.n && !cw.done()) {
        const uint64_t slo = (uint64_t)pos * 64;
        const uint64_t shi = (uint64_t)(pos + cw.seg()) * 64;
        if (r.end[i] <= slo) { ++i; continue; }
        if (r.start[i] >= shi) { pos += cw.seg(); cw.consume(cw.seg()); continue; }
        if (cw.in_fill() && !cw.fill_val && cw.seg() >= MINFILL) {
            while (i < r.n && (uint64_t)r.end[i] <= shi) ++i;
            pos += cw.seg(); cw.consume(cw.seg());
            continue;
        }
        const uint32_t a = (uint32_t)std::max<uint64_t>(r.start[i], slo);
        const uint32_t b = (uint32_t)std::min<uint64_t>(r.end[i],   shi);
        if (cw.in_fill()) { if (cw.fill_val) c += b - a; }
        else              c += range_in_words(cw.lit(), pos, a, b);
        if ((uint64_t)r.end[i] <= shi) ++i;
        else { pos += cw.seg(); cw.consume(cw.seg()); }
    }
    return c;
}

const Variant<fn_rw> kRW[] = {
    {"merge",    rw_merge,    "reference: runs against the segment stream"},
    {"merge2",   rw_merge2,   "single-pass two-cursor merge, no segment re-walking"},
    {"skip",     rw_skip,     "a zero fill retires every run inside it in one step"},
    {"adaptive", rw_adaptive, "skip form, with an empty-run-array early out"},
    {"skip2",    rw_skip2,    "zero fills retire runs; one-fills absorb them by add"},
};

/* ww_skip wins on 4 of 5 corpora but loses to ww_neon on dense data, where the
 * stream is all literals and the zero-fill branch never fires. This form keeps
 * the bulk skip and adds an early out for the case that makes it useless: if
 * NEITHER side has a fill in front of it, go straight to the vectorized literal
 * AND without re-testing the fill conditions. */
uint64_t ww_skip2(const EwahView& a, const EwahView& b) {
    uint64_t c = 0;
    Cursor ca(a), cb(b);
    while (!ca.done() && !cb.done()) {
        if (!ca.in_fill() && !cb.in_fill()) {              // the dense-data path
            const uint32_t k = std::min(ca.seg(), cb.seg());
            c += and_popcnt_words(ca.lit(), cb.lit(), k);
            ca.consume(k);
            cb.consume(k);
            continue;
        }
        if (ca.in_fill() && !ca.fill_val) {
            uint32_t k = ca.fill_left;
            while (k && !cb.done()) { const uint32_t t = std::min(k, cb.seg()); cb.consume(t); k -= t; }
            ca.consume(ca.fill_left - k);
            continue;
        }
        if (cb.in_fill() && !cb.fill_val) {
            uint32_t k = cb.fill_left;
            while (k && !ca.done()) { const uint32_t t = std::min(k, ca.seg()); ca.consume(t); k -= t; }
            cb.consume(cb.fill_left - k);
            continue;
        }
        const uint32_t k = std::min(ca.seg(), cb.seg());
        if (ca.in_fill() && cb.in_fill()) c += uint64_t(k) * 64;
        else if (ca.in_fill())            c += popcnt_words(cb.lit(), k);
        else                              c += popcnt_words(ca.lit(), k);
        ca.consume(k);
        cb.consume(k);
    }
    return c;
}

/* Branchless segment classification. ww_skip2 has a four-way branch per step on
 * (a is fill, b is fill); this collapses the two fill-vs-literal cases into one
 * path by treating a one-fill as an implicit all-ones literal source. */
uint64_t ww_bl(const EwahView& a, const EwahView& b) {
    uint64_t c = 0;
    Cursor ca(a), cb(b);
    while (!ca.done() && !cb.done()) {
        const uint32_t k = std::min(ca.seg(), cb.seg());
        const bool fa = ca.in_fill(), fb = cb.in_fill();
        const bool za = fa && !ca.fill_val, zb = fb && !cb.fill_val;
        if (!za && !zb) {
            if (fa && fb)        c += uint64_t(k) * 64;
            else if (fa)         c += popcnt_words(cb.lit(), k);
            else if (fb)         c += popcnt_words(ca.lit(), k);
            else                 c += and_popcnt_words(ca.lit(), cb.lit(), k);
        }
        ca.consume(k);
        cb.consume(k);
    }
    return c;
}

// Same question for W x W: the bulk skip that lets a zero fill swallow the
// other stream's segments is a loop, and MINFILL is where it beats stepping.
template <uint32_t MINFILL>
uint64_t ww_skip_t(const EwahView& a, const EwahView& b) {
    uint64_t c = 0;
    Cursor ca(a), cb(b);
    while (!ca.done() && !cb.done()) {
        if (ca.in_fill() && !ca.fill_val && ca.fill_left >= MINFILL) {
            uint32_t k = ca.fill_left;
            while (k && !cb.done()) { const uint32_t t = std::min(k, cb.seg()); cb.consume(t); k -= t; }
            ca.consume(ca.fill_left - k);
            continue;
        }
        if (cb.in_fill() && !cb.fill_val && cb.fill_left >= MINFILL) {
            uint32_t k = cb.fill_left;
            while (k && !ca.done()) { const uint32_t t = std::min(k, ca.seg()); ca.consume(t); k -= t; }
            cb.consume(cb.fill_left - k);
            continue;
        }
        const uint32_t k = std::min(ca.seg(), cb.seg());
        const bool fa = ca.in_fill(), fb = cb.in_fill();
        if (!(fa && !ca.fill_val) && !(fb && !cb.fill_val)) {
            if (fa && fb)  c += uint64_t(k) * 64;
            else if (fa)   c += popcnt_words(cb.lit(), k);
            else if (fb)   c += popcnt_words(ca.lit(), k);
            else           c += and_popcnt_words(ca.lit(), cb.lit(), k);
        }
        ca.consume(k); cb.consume(k);
    }
    return c;
}

const Variant<fn_ww> kWW[] = {
    {"scalar",   ww_scalar,   "reference: segment merge, word at a time"},
    {"neon",     ww_neon,     "NEON literal bodies"},
    {"skip",     ww_skip,     "a zero fill swallows the other side's segments whole"},
    {"skip2",    ww_skip2,    "bulk skip plus a literal-vs-literal fast path"},
    {"bl",       ww_bl,       "branchless segment classification, no bulk skip"},
    {"skip_f512",ww_skip_t<512>, "bulk skip above 512 words"},
};

} // namespace

VariantList<fn_bw> cell_bw() { return {kBW, sizeof(kBW) / sizeof(kBW[0])}; }
VariantList<fn_sw> cell_sw() { return {kSW, sizeof(kSW) / sizeof(kSW[0])}; }
VariantList<fn_rw> cell_rw() { return {kRW, sizeof(kRW) / sizeof(kRW[0])}; }
VariantList<fn_ww> cell_ww() { return {kWW, sizeof(kWW) / sizeof(kWW[0])}; }

} // namespace storm
