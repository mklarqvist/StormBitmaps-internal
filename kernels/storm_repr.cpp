/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist
 * Licensed under the Apache License, Version 2.0.
 *
 * Representation constructors. Correctness here is load-bearing for every
 * measurement in the project: if a converter is wrong, every cell that consumes
 * it is measuring the wrong thing. tests/test_cells.cpp round-trips all five.
 */
#include "kernels/storm_repr.h"

#include <cassert>
#include <algorithm>
#include <cstring>
#include <new>

namespace storm {

/* rank9-style two-level index. See BitmapView in storm_repr.h for the layout.
 *
 * The sentinel block at index nblk carries the total in its absolute slot, so
 * rank_at() can answer a query at the very end of the universe without a branch
 * on nw and rank_block_empty() can read blk+1 for the last real block without
 * running off the array. Costs 16 bytes; removes two edge cases from every
 * kernel that consumes the index. */
void build_rank(const uint64_t* words, uint32_t nw, avec<uint64_t>& out) {
    const uint32_t stride = BitmapView::RANK_STRIDE;
    const uint32_t nblk   = (nw + stride - 1) / stride;
    out.assign(2 * (size_t)(nblk + 1), 0);

    uint64_t acc = 0;
    for (uint32_t b = 0; b < nblk; ++b) {
        out[2 * b] = acc;
        uint64_t sub = 0, rel = 0;
        for (uint32_t k = 0; k < stride; ++k) {
            const uint32_t wi = b * stride + k;
            // Entry k-1 holds the count over words [8b, 8b+k), so it is written
            // BEFORE this word is folded in.
            if (k >= 1) sub |= (rel & 0x1FF) << (9 * (k - 1));
            if (wi < nw) rel += STORM_POPCOUNT(words[wi]);
        }
        out[2 * b + 1] = sub;
        acc += rel;
    }
    out[2 * nblk] = acc;   // sentinel: total
}

void build_occ(const uint64_t* words, uint32_t nw, avec<uint64_t>& out,
               uint32_t bin_words) {
    if (bin_words == 0) bin_words = BitmapView::OCC_BIN_WORDS_DEFAULT;
    const uint32_t bins = (nw + bin_words - 1) / bin_words;
    out.assign((bins + 63) / 64, 0);
    for (uint32_t b = 0; b < bins; ++b) {
        const uint32_t lo = b * bin_words;
        const uint32_t hi = std::min(lo + bin_words, nw);
        uint64_t any = 0;
        for (uint32_t k = lo; k < hi; ++k) any |= words[k];
        if (any) out[b >> 6] |= uint64_t(1) << (b & 63);
    }
}

void ewah_decode(const EwahView& w, uint64_t* out_words) {
    uint32_t o = 0;
    uint32_t i = 0;
    while (i < w.n) {
        const uint64_t m  = w.buf[i++];
        const uint64_t fl = ewah_fill_len(m);
        const uint64_t ll = ewah_lit_len(m);
        const uint64_t fv = ewah_fill_bit(m) ? ~uint64_t(0) : uint64_t(0);
        for (uint64_t k = 0; k < fl; ++k) out_words[o++] = fv;
        for (uint64_t k = 0; k < ll; ++k) out_words[o++] = w.buf[i++];
    }
    assert(o == w.nw);
    (void)o;
}

// Compress a bitmap to EWAH-64. A maximal stretch of identical all-zero or
// all-ones words becomes a fill; everything else becomes a literal.
static void build_ewah(const uint64_t* words, uint32_t nw, avec<uint64_t>& out) {
    out.clear();
    uint32_t i = 0;
    while (i < nw) {
        // Leading fill.
        bool     fv  = (words[i] == ~uint64_t(0));
        uint64_t fl  = 0;
        if (words[i] == 0 || fv) {
            const uint64_t pat = fv ? ~uint64_t(0) : uint64_t(0);
            while (i < nw && words[i] == pat && fl < 0x7FFFFFFFull) { ++i; ++fl; }
        } else {
            fv = false;   // no leading fill; a zero-length fill has value 0
        }
        // Following literals.
        const uint32_t lit0 = i;
        while (i < nw && words[i] != 0 && words[i] != ~uint64_t(0)) ++i;
        const uint64_t ll = i - lit0;

        out.push_back(ewah_marker(fv, fl, ll));
        for (uint64_t k = 0; k < ll; ++k) out.push_back(words[lit0 + k]);
    }
    if (out.empty()) out.push_back(ewah_marker(false, 0, 0));
}

void build_row(Row& out, const uint32_t* positions, size_t n, uint32_t universe,
               bool sparse_only) {
    const uint32_t nw = (universe + 63u) / 64u;

    out.list.assign(positions, positions + n);
    if (!sparse_only) out.bitmap.assign(nw, 0);
    else              out.bitmap.clear();

    for (size_t i = 0; i < n; ++i) {
        const uint32_t p = positions[i];
        assert(p < universe);
        assert(i == 0 || positions[i] > positions[i - 1]);   // sorted, distinct
        if (!sparse_only) out.bitmap[p >> 6] |= uint64_t(1) << (p & 63);
    }

    // Runs, straight off the sorted list — [start, end) with adjacent positions
    // merged, so end[i] < start[i+1] strictly.
    out.run_start.clear();
    out.run_end.clear();
    for (size_t i = 0; i < n; ) {
        size_t j = i;
        while (j + 1 < n && positions[j + 1] == positions[j] + 1) ++j;
        out.run_start.push_back(positions[i]);
        out.run_end.push_back(positions[j] + 1);
        i = j + 1;
    }

    if (!sparse_only) { build_ewah(out.bitmap.data(), nw, out.ewah); }
    else               { out.ewah.clear(); }
    out.ewah_nw = nw;

    /* Complement, built only when it pays: a row of density > 1/2 has a
     * smaller complement, and every pairing against it costs
     * Theta(|complement|) instead of Theta(m). Below half density this is dead
     * weight, so it is simply not built -- the decision is one comparison on
     * data already computed. */
    out.comp_list.clear(); out.comp_start.clear(); out.comp_end.clear();
    if (!sparse_only && (uint64_t)n * 2 > (uint64_t)universe) {
        uint32_t prev = 0;
        for (size_t i = 0; i < n; ++i) {
            for (uint32_t p = prev; p < positions[i]; ++p) out.comp_list.push_back(p);
            prev = positions[i] + 1;
        }
        for (uint32_t p = prev; p < universe; ++p) out.comp_list.push_back(p);
        for (size_t i = 0; i < out.comp_list.size(); ) {
            size_t j = i;
            while (j + 1 < out.comp_list.size() &&
                   out.comp_list[j + 1] == out.comp_list[j] + 1) ++j;
            out.comp_start.push_back(out.comp_list[i]);
            out.comp_end.push_back(out.comp_list[j] + 1);
            i = j + 1;
        }
    }

    uint32_t nnz = 0;
    if (!sparse_only) {
        build_rank(out.bitmap.data(), nw, out.rank);
        build_occ(out.bitmap.data(), nw, out.occ, out.occ_bin);
        for (uint32_t k = 0; k < nw; ++k) nnz += (out.bitmap[k] != 0);
    } else {
        out.rank.clear(); out.occ.clear();
        // Without the bitmap the exact live-word count is unavailable; the run
        // count bounds it and is exact when no two runs share a word.
        nnz = (uint32_t)out.run_start.size();
    }

    out.meta.cardinality = (uint32_t)n;
    out.meta.n_runs      = (uint32_t)out.run_start.size();
    out.meta.n_words     = nw;
    out.meta.n_nonzero_w = nnz;
    out.meta.first_set   = n ? positions[0]     : UINT32_MAX;
    out.meta.last_set    = n ? positions[n - 1] : 0;
}

} // namespace storm
