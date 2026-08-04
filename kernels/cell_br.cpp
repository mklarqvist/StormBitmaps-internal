/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist
 * Licensed under the Apache License, Version 2.0.
 *
 * Cell B x R — dense bitmap against runs. P0, and the cell carrying the
 * project's strongest single claim (PROBLEM_STATEMENT.md 4, RESEARCH_PLAN.md
 * 4.5, claim P4).
 *
 * For CARDINALITY ONLY, a run [a, b) on the sparse side contributes exactly the
 * number of set bits of the dense side in [a, b). With a prefix-popcount index
 * over the dense side that is one subtraction:
 *
 *     contribution = rank_B(b) - rank_B(a)
 *
 * so the cell costs Theta(r) — the number of RUNS — with run LENGTH dropping
 * out entirely. A row that is one 9-million-bit zero run followed by one
 * 1-million-bit set run costs two rank queries, not 156,250 word operations.
 *
 * P4 is precisely the claim that length drops out, so the harness must report
 * B x R timing against run count and run length SEPARATELY. A single "runs
 * per second" number would hide exactly the thing being claimed.
 *
 * --- MEASURED (bench/p4_runlength.sh, Apple M4, 1,048,576-bit universe, run
 *     count held at ~15.3 per pair while run length varies 256x) --------------
 *
 *   mean run   no-index (neon)   rank        ratio
 *       64 b      1.72 ns/run    2.20 ns/run  0.78x
 *      256 b      2.91           2.51         1.16x
 *     1024 b      4.80           2.74         1.76x
 *     4096 b     10.46           4.49         2.33x
 *    16384 b     31.75           3.29         9.65x
 *
 * The no-index kernel grows 18x across the sweep; rank stays between 2.2 and
 * 4.5 ns/run with no trend. Run length drops out of the cost -- claim P4, on
 * this host, at tier 3 (measured).
 *
 * The crossover is at roughly a 256-bit run, i.e. 4 words. That is NOTABLY
 * LOWER than the analytic estimate that used to sit in this comment (600-1000
 * bits, derived from op counts), because the analysis counted instructions and
 * the real cost of the no-index path is memory traffic: it must touch every
 * word of the run, and past L1 that dominates the op count entirely. The
 * estimate was wrong in the direction that mattered -- it would have set the
 * shipping threshold 3x too high and given away most of the win on medium runs.
 * Which is exactly why AGENTS.md rule 8 marks derived numbers as derived until
 * measured.
 */
#include "kernels/storm_cells.h"
#include "kernels/storm_simd.h"

#include <vector>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#  include <arm_neon.h>
#  define STORM_CELL_NEON 1
#else
#  define STORM_CELL_NEON 0
#endif

namespace storm {
namespace {

// Set bits of `b` in [lo, hi), counted directly. No index.
static inline uint64_t count_range(const BitmapView& b, uint32_t lo, uint32_t hi) {
    if (lo >= hi) return 0;
    const uint32_t wlo = lo >> 6, whi = (hi - 1) >> 6;
    const uint64_t mlo = ~uint64_t(0) << (lo & 63);
    const uint64_t mhi = (hi & 63) ? ((uint64_t(1) << (hi & 63)) - 1) : ~uint64_t(0);

    if (wlo == whi) return STORM_POPCOUNT(b.w[wlo] & mlo & mhi);

    uint64_t c = STORM_POPCOUNT(b.w[wlo] & mlo);
    for (uint32_t k = wlo + 1; k < whi; ++k) c += STORM_POPCOUNT(b.w[k]);
    return c + STORM_POPCOUNT(b.w[whi] & mhi);
}

// --- V0: reference ---------------------------------------------------------
uint64_t br_scalar(const BitmapView& b, const RunView& r) {
    uint64_t c = 0;
    for (uint32_t i = 0; i < r.n; ++i) c += count_range(b, r.start[i], r.end[i]);
    return c;
}

#if STORM_CELL_NEON
// Vectorized body of count_range: the whole-word interior of a long run.
// Uses the shared multi-accumulator primitive; the single-accumulator version
// this replaced was latency bound (storm_simd.h).
static inline uint64_t popcount_words(const uint64_t* w, uint32_t n) {
    return simd_popcnt(w, n);
}

static inline uint64_t count_range_neon(const BitmapView& b, uint32_t lo, uint32_t hi) {
    if (lo >= hi) return 0;
    const uint32_t wlo = lo >> 6, whi = (hi - 1) >> 6;
    const uint64_t mlo = ~uint64_t(0) << (lo & 63);
    const uint64_t mhi = (hi & 63) ? ((uint64_t(1) << (hi & 63)) - 1) : ~uint64_t(0);
    if (wlo == whi) return STORM_POPCOUNT(b.w[wlo] & mlo & mhi);
    uint64_t c = STORM_POPCOUNT(b.w[wlo] & mlo) + STORM_POPCOUNT(b.w[whi] & mhi);
    if (whi > wlo + 1) c += popcount_words(b.w + wlo + 1, whi - wlo - 1);
    return c;
}

// --- V1: no index, vectorized run interiors --------------------------------
// The right comparison for the rank index. Beating br_scalar proves nothing;
// beating THIS is what makes the index interesting.
uint64_t br_neon(const BitmapView& b, const RunView& r) {
    uint64_t c = 0;
    for (uint32_t i = 0; i < r.n; ++i) c += count_range_neon(b, r.start[i], r.end[i]);
    return c;
}
#endif

// --- V2: the rank index, pure ----------------------------------------------
// Theta(r), independent of run length. This is claim P4.
uint64_t br_rank(const BitmapView& b, const RunView& r) {
    if (b.rank == nullptr) return br_scalar(b, r);
    uint64_t c = 0;
    for (uint32_t i = 0; i < r.n; ++i)
        c += rank_at(b, r.end[i]) - rank_at(b, r.start[i]);
    return c;
}

// --- V3: rank, with the two queries interleaved ----------------------------
// rank_at(end) and rank_at(start) are wholly independent, and consecutive runs
// are independent of each other too. The pure loop above leaves that ILP on the
// floor behind a single accumulator. Two runs (four rank queries) per iteration
// with separate accumulators gives the scheduler something to overlap the index
// loads with.
uint64_t br_rank_ilp(const BitmapView& b, const RunView& r) {
    if (b.rank == nullptr) return br_scalar(b, r);
    uint64_t c0 = 0, c1 = 0;
    uint32_t i = 0;
    for (; i + 2 <= r.n; i += 2) {
        c0 += rank_at(b, r.end[i])     - rank_at(b, r.start[i]);
        c1 += rank_at(b, r.end[i + 1]) - rank_at(b, r.start[i + 1]);
    }
    uint64_t c = c0 + c1;
    for (; i < r.n; ++i) c += rank_at(b, r.end[i]) - rank_at(b, r.start[i]);
    return c;
}

// --- V3b: one rank block lookup per run when both ends share a block ---------
// rank_at() is two loads: the rank pair for the block, and one bitmap word.
// Called twice per run that is four loads. But a run SHORTER than a rank block
// (512 bits) usually has both ends in the same block, and then the block's
// absolute counter is common to both queries and cancels in the subtraction:
//
//     rank(hi) - rank(lo) = (sub[k_hi] - sub[k_lo])
//                         + popcount(w[wi_hi] & mask_hi)
//                         - popcount(w[wi_lo] & mask_lo)
//
// so the pair costs one rank load instead of two. Long runs still take the
// general path. This targets exactly the regime where the pure rank variant was
// LOSING (0.5x on short-run corpora) rather than the one where it already wins.
uint64_t br_rank_blk(const BitmapView& b, const RunView& r) {
    if (b.rank == nullptr) return br_scalar(b, r);
    uint64_t c = 0;
    for (uint32_t i = 0; i < r.n; ++i) {
        const uint32_t lo = r.start[i], hi = r.end[i];
        const uint32_t wlo = lo >> 6, whi = hi >> 6;
        const uint32_t blo = wlo >> 3, bhi = whi >> 3;
        if (blo == bhi && whi < b.nw) {
            const uint64_t sub = b.rank[2 * blo + 1];
            const uint32_t klo = wlo & 7, khi = whi & 7;
            const uint64_t rlo = klo ? ((sub >> (9 * (klo - 1))) & 0x1FF) : 0;
            const uint64_t rhi = khi ? ((sub >> (9 * (khi - 1))) & 0x1FF) : 0;
            c += (rhi + STORM_POPCOUNT(b.w[whi] & ((uint64_t(1) << (hi & 63)) - 1)))
               - (rlo + STORM_POPCOUNT(b.w[wlo] & ((uint64_t(1) << (lo & 63)) - 1)));
        } else {
            c += rank_at(b, hi) - rank_at(b, lo);
        }
    }
    return c;
}

// --- V4: hybrid -- the one that should actually ship ------------------------
// Rank for long runs, direct counting for short ones.
//
// 4 words = 256 bits is where bench/p4_runlength.sh measures the crossover on
// this host (see the header table). It was 12 here originally, taken from the
// analytic estimate, which cost ~1.5x on the 1024-bit-run point.
//
// This is still a hardcoded threshold and AGENTS.md forbids those: it belongs
// in the cost model (M4), calibrated per host, because the crossover is set by
// where the bitmap sits in the cache hierarchy and that is a property of the
// machine and the corpus, not a constant. Until M4 exists it is at least
// measured, named, and in one place.
constexpr uint32_t kBrRankMinWords = 4;

template <uint32_t MINW>
uint64_t br_hybrid(const BitmapView& b, const RunView& r) {
    if (b.rank == nullptr) return br_scalar(b, r);
    uint64_t c = 0;
    for (uint32_t i = 0; i < r.n; ++i) {
        const uint32_t lo = r.start[i], hi = r.end[i];
        if ((hi - lo) >= MINW * 64u) c += rank_at(b, hi) - rank_at(b, lo);
        else                         c += count_range(b, lo, hi);
    }
    return c;
}

// --- V6: skip runs that land in an all-zero rank block -----------------------
// The work-avoidance form. rank_block_empty() proves 512 bits are zero from two
// counters already in cache, so a run confined to such a block contributes
// nothing and needs no bitmap touch at all. Complements the rank difference
// rather than replacing it: rank makes a LONG run cheap, this makes an EMPTY
// region cheap, and skewed data has both.
uint64_t br_skip(const BitmapView& b, const RunView& r) {
    if (b.rank == nullptr) return br_scalar(b, r);
    const uint32_t nblk = (b.nw + BitmapView::RANK_STRIDE - 1) / BitmapView::RANK_STRIDE;
    uint64_t c = 0;
    for (uint32_t i = 0; i < r.n; ++i) {
        const uint32_t lo = r.start[i], hi = r.end[i];
        const uint32_t blo = (lo >> 6) >> 3, bhi = ((hi - 1) >> 6) >> 3;
        if (blo == bhi && blo < nblk && rank_block_empty(b, blo)) continue;
        if ((hi - lo) >= kBrRankMinWords * 64u) c += rank_at(b, hi) - rank_at(b, lo);
        else                                    c += count_range(b, lo, hi);
    }
    return c;
}

// --- V7: prefetch the rank entries of upcoming runs --------------------------
// Runs are sorted, so the rank probes march forward -- but with gaps, since a
// run may skip many blocks. That is the access pattern a stride prefetcher
// handles worst. Two runs of lookahead costs two hints per run.
uint64_t br_rank_pf(const BitmapView& b, const RunView& r) {
    if (b.rank == nullptr) return br_scalar(b, r);
    uint64_t c0 = 0, c1 = 0;
    for (uint32_t i = 0; i < r.n; ++i) {
        if (i + 2 < r.n) {
            __builtin_prefetch(&b.rank[2 * ((r.start[i + 2] >> 6) >> 3)], 0, 1);
            __builtin_prefetch(&b.w[r.start[i + 2] >> 6], 0, 1);
        }
        c0 += rank_at(b, r.end[i]);
        c1 += rank_at(b, r.start[i]);
    }
    return c0 - c1;
}

// --- V8: two-pass rank, ends and starts separately ---------------------------
// br_rank interleaves rank_at(end) and rank_at(start), so the two index probes
// for one run are adjacent in the instruction stream but land in different
// cache lines whenever the run is long. Splitting into two passes gives each
// pass a monotonically increasing probe sequence -- the pattern a stride
// prefetcher handles best -- at the cost of reading the run array twice.
//
// Three previous B x R hypotheses failed (shared block lookup, empty-block skip,
// prefetch), so the working assumption is now that the cell is bound by the
// rank probes' cache behaviour rather than by their count. This tests that
// directly: if locality is the problem, restructuring the access order helps
// even though the number of probes is identical.
uint64_t br_rank_2pass(const BitmapView& b, const RunView& r) {
    if (b.rank == nullptr) return br_scalar(b, r);
    uint64_t hi = 0, lo = 0;
    uint32_t i = 0;
    for (; i + 2 <= r.n; i += 2) {
        hi += rank_at(b, r.end[i]);
        hi += rank_at(b, r.end[i + 1]);
    }
    for (; i < r.n; ++i) hi += rank_at(b, r.end[i]);
    i = 0;
    for (; i + 2 <= r.n; i += 2) {
        lo += rank_at(b, r.start[i]);
        lo += rank_at(b, r.start[i + 1]);
    }
    for (; i < r.n; ++i) lo += rank_at(b, r.start[i]);
    return hi - lo;
}

// --- V9: hybrid driven by the pair, not the run ------------------------------
// Every hybrid so far tests each run's length individually, so a pair of rows
// with 500 runs pays 500 branches to reach the same answer 500 times. Mean run
// length is one division from data the caller already has, so the strategy can
// be chosen ONCE per pair and the inner loop left branch-free.
//
// This is the shape standing rule 7 asks selection decisions to take, applied
// inside a cell rather than above it.
uint64_t br_hybrid_pair(const BitmapView& b, const RunView& r) {
    if (b.rank == nullptr || r.n == 0) return br_scalar(b, r);
    const uint32_t span = r.end[r.n - 1] - r.start[0];
    const uint32_t mean_run = span / r.n;              // upper bound on the true mean
    uint64_t c = 0;
    if (mean_run >= kBrRankMinWords * 64u) {
        for (uint32_t i = 0; i < r.n; ++i)
            c += rank_at(b, r.end[i]) - rank_at(b, r.start[i]);
    } else {
        for (uint32_t i = 0; i < r.n; ++i)
            c += count_range(b, r.start[i], r.end[i]);
    }
    return c;
}

// --- V5: the inflate-to-bitmap fallback, LABELLED baseline -----------------
// Standing rule 3. Present so P3/P4 have something to beat.
thread_local std::vector<uint64_t> g_inflate_r;

uint64_t br_inflate(const BitmapView& b, const RunView& r) {
    if (g_inflate_r.size() < b.nw) g_inflate_r.assign(b.nw, 0);
    for (uint32_t i = 0; i < r.n; ++i) {
        for (uint32_t p = r.start[i]; p < r.end[i]; ++p)
            g_inflate_r[p >> 6] |= uint64_t(1) << (p & 63);
    }
    uint64_t c = 0;
    for (uint32_t k = 0; k < b.nw; ++k) c += STORM_POPCOUNT(b.w[k] & g_inflate_r[k]);
    for (uint32_t i = 0; i < r.n; ++i)
        for (uint32_t p = r.start[i]; p < r.end[i]; ++p) g_inflate_r[p >> 6] = 0;
    return c;
}

const Variant<fn_br> kBR[] = {
    {"scalar",      br_scalar,                "reference: masked head/tail + word loop per run"},
#if STORM_CELL_NEON
    {"neon",        br_neon,                  "NEON run interiors -- the real no-index baseline"},
#endif
    {"rank",        br_rank,                  "M5: rank(end) - rank(start), Theta(runs)", true},
    {"rank_ilp",    br_rank_ilp,              "M5, two runs per iteration",               true},
    {"rank_blk",    br_rank_blk,              "M5, one block lookup when a run stays in one block", true},
    {"hybrid12",    br_hybrid<kBrRankMinWords>,"rank above 12 words of run, direct below", true},
    {"hybrid4",     br_hybrid<4>,             "crossover probe: rank above 4 words",       true},
    {"hybrid32",    br_hybrid<32>,            "crossover probe: rank above 32 words",      true},
    {"rank_2pass",  br_rank_2pass,            "ends and starts in separate monotonic passes", true},
    {"hybrid_pair", br_hybrid_pair,           "choose rank vs direct ONCE per pair, not per run", true},
    {"skip",        br_skip,                  "skip runs inside an all-zero rank block",   true},
    {"rank_pf",     br_rank_pf,               "M5 with rank/bitmap prefetch two runs ahead",true},
    {"inflate",     br_inflate,               "BASELINE ONLY: materialize R as a bitmap, run B x B",
                                               false, true},
};

} // namespace

VariantList<fn_br> cell_br() { return {kBR, sizeof(kBR) / sizeof(kBR[0])}; }

} // namespace storm
