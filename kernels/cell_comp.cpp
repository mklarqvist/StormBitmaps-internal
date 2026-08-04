/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist. Apache-2.0.
 *
 * Cell C x B — the complemented pairing.
 *
 * The dense-tail mirror of the sparse cells. For A stored as its complement A':
 *
 *     |A n B| = |B| - |A' n B|
 *
 * so the cost is Theta(|A'|) -- proportional to how far A is from FULL, exactly
 * as B x S costs Theta(|A|) proportional to how far A is from empty. The
 * density curve is symmetric about 1/2 (RESEARCH_PLAN.md 14, claim C3) and this
 * is the cell that makes the right half of it deliberate rather than accidental.
 *
 * `|B|` is `rank`'s sentinel total, already computed; no extra work.
 *
 * RESEARCH_PLAN.md 13.3: no library or paper was found that does this
 * systematically for intersection cardinality. EWAH treats 0-runs and 1-runs
 * symmetrically at the REPRESENTATION level and Roaring exposes flip for range
 * construction, but neither states the identity above as a cardinality
 * algorithm. This cell is the paper's strongest unclaimed contribution.
 */
#include "kernels/storm_cells.h"
#include "kernels/storm_simd.h"

namespace storm {
namespace {

inline uint64_t card_of(const BitmapView& b) {
    if (b.rank) return b.rank[2 * ((b.nw + 7) / 8)];
    return simd_popcnt(b.w, b.nw);
}

// The complement as a sorted position list: one probe per absent bit.
uint64_t cb_list(const ComplementView& a, const BitmapView& b) {
    if (!a.valid) return 0;
    uint64_t hit = 0;
    uint64_t a0 = 0, a1 = 0, a2 = 0, a3 = 0;
    uint32_t i = 0;
    for (; i + 4 <= a.s.n; i += 4) {
        const uint32_t v0 = a.s.v[i], v1 = a.s.v[i+1], v2 = a.s.v[i+2], v3 = a.s.v[i+3];
        a0 += (b.w[v0 >> 6] >> (v0 & 63)) & 1u;
        a1 += (b.w[v1 >> 6] >> (v1 & 63)) & 1u;
        a2 += (b.w[v2 >> 6] >> (v2 & 63)) & 1u;
        a3 += (b.w[v3 >> 6] >> (v3 & 63)) & 1u;
    }
    hit = (a0 + a1) + (a2 + a3);
    for (; i < a.s.n; ++i) hit += (b.w[a.s.v[i] >> 6] >> (a.s.v[i] & 63)) & 1u;
    return card_of(b) - hit;
}

// The complement as runs, with the rank index: Theta(complement runs), and
// independent of how long those runs are -- the P4 mechanism applied to the
// dense tail.
uint64_t cb_runs(const ComplementView& a, const BitmapView& b) {
    if (!a.valid) return 0;
    uint64_t hit = 0;
    if (b.rank) {
        for (uint32_t i = 0; i < a.r.n; ++i)
            hit += rank_at(b, a.r.end[i]) - rank_at(b, a.r.start[i]);
    } else {
        for (uint32_t i = 0; i < a.r.n; ++i)
            for (uint32_t p = a.r.start[i]; p < a.r.end[i]; ++p)
                hit += (b.w[p >> 6] >> (p & 63)) & 1u;
    }
    return card_of(b) - hit;
}

// Pick by complement shape, the same O(1) metadata decision the other cells use.
uint64_t cb_adaptive(const ComplementView& a, const BitmapView& b) {
    if (!a.valid) return 0;
    if (a.r.n && a.s.n / (a.r.n ? a.r.n : 1) >= 4) return cb_runs(a, b);
    return cb_list(a, b);
}

const Variant<fn_cb> kCB[] = {
    {"list",     cb_list,     "|B| - |A' n B| with A' as positions: Theta(|complement|)"},
    {"runs",     cb_runs,     "A' as runs + rank index: Theta(complement runs)", true},
    {"adaptive", cb_adaptive, "pick on the complement's run/position ratio"},
};

} // namespace

VariantList<fn_cb> cell_cb() { return {kCB, sizeof(kCB) / sizeof(kCB[0])}; }

} // namespace storm
