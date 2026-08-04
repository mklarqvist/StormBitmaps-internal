/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist
 * Licensed under the Apache License, Version 2.0.
 *
 * The representation layer (RESEARCH_PLAN.md Phase 0, "representation layer").
 *
 * Five representations of one row of a binary matrix, over a universe of
 * `universe` bits:
 *
 *   B  bitmap          — dense words, Theta(m) per pairing regardless of density
 *   S  sorted array    — ascending distinct positions
 *   R  run/RLE         — maximal runs of set bits, [start, end)
 *   W  WAH-style fills — EWAH-64 (see below)
 *   Ro Roaring         — meta; per-chunk dispatch, not built here
 *
 * This header is C++17 and INTERNAL. It is not part of the C ABI and must not
 * be included from storm.h. See AGENTS.md, "Language and ABI".
 *
 * --- Why EWAH-64 for W -------------------------------------------------------
 * Classic WAH packs a 31-bit literal into a 32-bit word, so literals are not
 * word-aligned with respect to the 64-bit bitmap words every other kernel uses.
 * That misalignment would force a shift on every literal in B x W and S x W and
 * would confound the measurement we actually care about. EWAH (Lemire et al.)
 * keeps literals as full machine words and hoists the fill bookkeeping into a
 * separate marker word, so a W literal is bit-identical to a B word. The fill
 * semantics — the thing the cell is testing — are unchanged.
 *
 * Marker word layout (64-bit):
 *   bit  0        fill value (0 or 1)
 *   bits 1..31    number of fill words   (31 bits)
 *   bits 32..63   number of literal words that follow (32 bits)
 */
#ifndef STORM_REPR_H_
#define STORM_REPR_H_

#include <cstdint>
#include <cstddef>
#include <vector>

#include "libalgebra/libalgebra.h"

namespace storm {

// ---------------------------------------------------------------------------
// Aligned storage. SIMD kernels assume 64-byte alignment of every buffer they
// are handed; std::vector's default allocator makes no over-alignment promise.
// ---------------------------------------------------------------------------
template <typename T, size_t Align = 64>
struct aligned_allocator {
    using value_type = T;
    aligned_allocator() noexcept = default;
    template <typename U> aligned_allocator(const aligned_allocator<U, Align>&) noexcept {}

    T* allocate(size_t n) {
        if (n == 0) return nullptr;
        void* p = STORM_aligned_malloc(Align, n * sizeof(T));
        if (p == nullptr) throw std::bad_alloc();
        return static_cast<T*>(p);
    }
    void deallocate(T* p, size_t) noexcept { if (p) STORM_aligned_free(p); }
    template <typename U> struct rebind { using other = aligned_allocator<U, Align>; };
    bool operator==(const aligned_allocator&) const noexcept { return true; }
    bool operator!=(const aligned_allocator&) const noexcept { return false; }
};

template <typename T> using avec = std::vector<T, aligned_allocator<T>>;

// ---------------------------------------------------------------------------
// Views. Kernels take views, never owners — so a kernel can be handed a slice
// of a packed corpus without a copy.
// ---------------------------------------------------------------------------

/* B — dense bitmap.
 *
 * `rank` is the optional prefix-popcount index of RESEARCH_PLAN.md 4.5 (M5) —
 * the thing that makes B x R cost Theta(runs) instead of Theta(bits).
 *
 * Layout is rank9-style (Vigna), two 64-bit words per 512-bit block:
 *
 *   rank[2b]     absolute: set bits in words [0, 8b)
 *   rank[2b + 1] packed relative: nine bits per sub-word, entry k-1 (k = 1..7)
 *                holds the set bits in words [8b, 8b + k)
 *
 * Seven 9-bit fields fit one word, and the largest value is 7 * 64 = 448 < 512,
 * so nothing is truncated. The point of the packed second word is that rank at
 * an arbitrary bit costs ONE popcount rather than a loop over up to seven words
 * — which moves the break-even against direct counting from ~1024 bits of run
 * length down to ~600, and that shift is what decides whether the index is
 * worth building at all.
 *
 * Space is 16 bytes per 64 bytes of bitmap = 25% overhead, the standard
 * constant-time-rank price. `build_rank` is separate from `build_row` so this
 * cost can be measured on its own. */
/* `occ` is a Parquet/ORC-style ZONE MAP over the bitmap: one bit per bin of
 * OCC_BIN_WORDS words, set when that bin holds any set bit at all.
 *
 * It is a different trade from `rank` and the two are complements, not rivals:
 *
 *              overhead     answers
 *   rank9      25%          "how many bits are set below position x" -- exact
 *   occ        0.195%       "is this 512-bit bin empty" -- one bit
 *
 * The zone map is 512x smaller than the data it summarizes, which is the whole
 * point: `occ_A & occ_B` is a bitmap intersection over m/512 bits, so the
 * question "can these two rows overlap anywhere?" costs 1/512 of the pairing it
 * would replace, and a popcount of that AND says how many bins even need
 * visiting. Rows that cannot overlap are settled without touching the data at
 * all, and rows that can are visited only where both sides have content.
 *
 * OCC_BIN_WORDS is set to RANK_STRIDE so a zone-map bit and a rank block cover
 * the same region and a kernel can use either without a second index geometry. */
struct BitmapView {
    const uint64_t* w     = nullptr;
    uint32_t        nw    = 0;         // words
    const uint64_t* rank  = nullptr;   // may be null
    const uint64_t* occ   = nullptr;   // may be null
    uint32_t        n_occ = 0;         // words in occ

    static constexpr uint32_t RANK_STRIDE   = 8;   // words per rank block (512 bits)
    static constexpr uint32_t OCC_BIN_WORDS = 8;   // words per zone-map bin
};

// Number of set bits strictly below bit position x. Requires b.rank != null.
inline uint64_t rank_at(const BitmapView& b, uint32_t x) {
    const uint32_t wi = x >> 6;
    if (wi >= b.nw) return b.rank[2 * ((b.nw + 7) / 8)];   // total, stored as the sentinel block
    const uint32_t blk = wi >> 3;
    const uint32_t k   = wi & 7;
    uint64_t r = b.rank[2 * blk];
    if (k) r += (b.rank[2 * blk + 1] >> (9 * (k - 1))) & 0x1FF;
    return r + STORM_POPCOUNT(b.w[wi] & ((uint64_t(1) << (x & 63)) - 1));
}

// True when words [8*blk, 8*blk+8) are entirely zero. O(1), no bitmap touch.
inline bool rank_block_empty(const BitmapView& b, uint32_t blk) {
    return b.rank[2 * blk] == b.rank[2 * (blk + 1)];
}

/* S — sorted, distinct, ascending bit positions. */
struct ListView {
    const uint32_t* v = nullptr;
    uint32_t        n = 0;
};

/* R — maximal runs of set bits, [start, end), ascending, non-overlapping and
 * non-adjacent (adjacent runs are merged at construction, so `end[i] < start[i+1]`
 * strictly). Structure-of-arrays: the run-merge kernels stride over `start` and
 * `end` independently, and SoA keeps each stream unit-stride. */
struct RunView {
    const uint32_t* start = nullptr;
    const uint32_t* end   = nullptr;  // exclusive
    uint32_t        n     = 0;        // number of runs
};

/* W — EWAH-64. */
struct EwahView {
    const uint64_t* buf = nullptr;
    uint32_t        n   = 0;          // words in buf (markers + literals)
    uint32_t        nw  = 0;          // decoded length in 64-bit words
};

// EWAH marker accessors.
inline bool     ewah_fill_bit (uint64_t m) { return  m & 1u; }
inline uint64_t ewah_fill_len (uint64_t m) { return (m >> 1) & 0x7FFFFFFFull; }
inline uint64_t ewah_lit_len  (uint64_t m) { return  m >> 32; }
inline uint64_t ewah_marker(bool fill_bit, uint64_t fill_len, uint64_t lit_len) {
    return (uint64_t)fill_bit | (fill_len << 1) | (lit_len << 32);
}

// ---------------------------------------------------------------------------
// M1 — per-row metadata. Everything the selection model (M2) is allowed to look
// at, and nothing that costs more than O(1) to read.
// ---------------------------------------------------------------------------
struct RowMeta {
    uint32_t cardinality = 0;   // |X|
    uint32_t n_runs      = 0;   // maximal runs of set bits
    uint32_t n_words     = 0;   // bitmap words in the universe
    uint32_t n_nonzero_w = 0;   // bitmap words that are not all-zero
    uint32_t first_set   = 0;   // first set bit  (UINT32_MAX if empty)
    uint32_t last_set    = 0;   // last  set bit  (0 if empty)
};

// ---------------------------------------------------------------------------
// Owning row. Holds every representation of one row so a cell benchmark can
// pick two of them without rebuilding.
// ---------------------------------------------------------------------------
struct Row {
    avec<uint64_t> bitmap;
    avec<uint64_t> rank;      // prefix popcount, stride BitmapView::RANK_STRIDE
    avec<uint64_t> occ;       // zone map, 1 bit per OCC_BIN_WORDS words
    avec<uint32_t> list;
    avec<uint32_t> run_start;
    avec<uint32_t> run_end;
    avec<uint64_t> ewah;
    uint32_t       ewah_nw = 0;
    RowMeta        meta;

    BitmapView B() const {
        return BitmapView{bitmap.data(), meta.n_words,
                          rank.empty() ? nullptr : rank.data(),
                          occ.empty()  ? nullptr : occ.data(),
                          (uint32_t)occ.size()};
    }
    // No auxiliary indexes at all: the fallback path every index-consuming
    // kernel must still be correct on.
    BitmapView B_norank() const {
        return BitmapView{bitmap.data(), meta.n_words, nullptr, nullptr, 0};
    }
    ListView   S() const { return ListView{list.data(), (uint32_t)list.size()}; }
    RunView    R() const { return RunView{run_start.data(), run_end.data(),
                                          (uint32_t)run_start.size()}; }
    EwahView   W() const { return EwahView{ewah.data(), (uint32_t)ewah.size(), ewah_nw}; }
};

// Build every representation of a row from a sorted, distinct position list.
// `universe` is rounded up to a whole number of 64-bit words.
void build_row(Row& out, const uint32_t* positions, size_t n, uint32_t universe);

// Decode a W back to a bitmap. Used by the oracle and by the inflate baselines.
void ewah_decode(const EwahView& w, uint64_t* out_words);

// Build the rank index over an existing bitmap. Separated from build_row so its
// construction cost can be measured on its own (RESEARCH_PLAN.md 4.5:
// "when is building the index worth it?").
void build_rank(const uint64_t* words, uint32_t nw, avec<uint64_t>& out);

// Build the zone map. Separate from build_row for the same reason as
// build_rank: its construction cost is a reportable quantity.
void build_occ(const uint64_t* words, uint32_t nw, avec<uint64_t>& out);

// Number of 512-bit bins in which BOTH rows have content. Zero proves the rows
// are disjoint. Costs one pass over m/512 bits.
inline uint64_t occ_overlap(const BitmapView& a, const BitmapView& b) {
    const uint32_t n = a.n_occ < b.n_occ ? a.n_occ : b.n_occ;
    uint64_t c = 0;
    for (uint32_t i = 0; i < n; ++i) c += STORM_POPCOUNT(a.occ[i] & b.occ[i]);
    return c;
}

} // namespace storm

#endif // STORM_REPR_H_
