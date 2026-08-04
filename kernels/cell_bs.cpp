/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist
 * Licensed under the Apache License, Version 2.0.
 *
 * Cell B x S — dense bitmap against sorted position list. Theta(|S|), and P0.
 *
 * This is the cell RESEARCH_PLAN.md 4 calls "the highest-value work in the
 * project": it sits on the seam between two literatures. SIMD set-intersection
 * papers cover list x list; popcount papers cover bitmap x bitmap; the mixed
 * case is the one nobody vectorized, and it is ~18% of pairs on skewed data.
 *
 * The four designs of RESEARCH_PLAN.md 4 are D1 gather, D2 run collapsing,
 * D3 conflict detection, D4 software-pipelined scalar. D1 and D3 are x86
 * constructs -- NEON has neither a gather nor VPCONFLICTD -- so on this host
 * the field is D2, D4, and whatever ILP restructuring buys. That is not a gap
 * in the plan so much as a finding about it: two of its four candidate designs
 * are not portable, and the portable ones are the ones that exploit data
 * structure rather than an instruction.
 *
 * Intrinsics used here are baseline AArch64 (see cell_bb.cpp preflight);
 * no gather is available and none is faked.
 */
#include "kernels/storm_cells.h"

#include <vector>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#  include <arm_neon.h>
#  define STORM_CELL_NEON 1
#else
#  define STORM_CELL_NEON 0
#endif

namespace storm {
namespace {

// --- V0: the reference, and what storm.cpp ships ---------------------------
// One bit test per list element. This is STORM_bitmap_x_list, and it is the
// honest baseline the vector designs must beat (RESEARCH_PLAN.md 4, D4).
uint64_t bs_scalar(const BitmapView& b, const ListView& s) {
    uint64_t c = 0;
    for (uint32_t i = 0; i < s.n; ++i)
        c += (b.w[s.v[i] >> 6] & (uint64_t(1) << (s.v[i] & 63))) != 0;
    return c;
}

// --- V1: branchless shift form ---------------------------------------------
// (w >> bit) & 1 instead of (w & (1 << bit)) != 0. Saves the materialization of
// the mask and the compare-to-zero; the shift amount is variable either way.
uint64_t bs_shift(const BitmapView& b, const ListView& s) {
    uint64_t c = 0;
    for (uint32_t i = 0; i < s.n; ++i)
        c += (b.w[s.v[i] >> 6] >> (s.v[i] & 63)) & 1u;
    return c;
}

// --- V2: independent accumulator chains ------------------------------------
// Every element's load is independent, but a single `c +=` serializes the adds
// and, more importantly, gives the scheduler one dependence chain to hide the
// load latency behind. U chains let U loads be in flight. On a sparse
// unclustered list the loads stride widely and this is the whole game.
template <int U>
uint64_t bs_ilp(const BitmapView& b, const ListView& s) {
    uint64_t acc[U] = {0};
    uint32_t i = 0;
    for (; i + U <= s.n; i += U)
        for (int u = 0; u < U; ++u)
            acc[u] += (b.w[s.v[i + u] >> 6] >> (s.v[i + u] & 63)) & 1u;
    uint64_t c = 0;
    for (int u = 0; u < U; ++u) c += acc[u];
    for (; i < s.n; ++i) c += (b.w[s.v[i] >> 6] >> (s.v[i] & 63)) & 1u;
    return c;
}

// --- V3: D2, run collapsing ------------------------------------------------
// A sorted list has many consecutive values sharing a 64-bit word. Fold each
// maximal same-word group into ONE mask, then one load + AND + popcount for the
// whole group. Cost drops from |S| loads to (distinct words touched) loads.
//
// This is the design RESEARCH_PLAN.md 4 predicts "most likely to win on
// realistic input and most likely to be missed by a synthetic benchmark",
// because uniform-random positions almost never share a word while real
// clustered data constantly does. The harness sweeps clustering precisely so
// this cannot be measured away.
uint64_t bs_collapse(const BitmapView& b, const ListView& s) {
    uint64_t c = 0;
    uint32_t i = 0;
    while (i < s.n) {
        const uint32_t w = s.v[i] >> 6;
        uint64_t mask = 0;
        do {
            mask |= uint64_t(1) << (s.v[i] & 63);
            ++i;
        } while (i < s.n && (s.v[i] >> 6) == w);
        c += STORM_POPCOUNT(b.w[w] & mask);
    }
    return c;
}

// --- V4: D2 with the group loop unrolled by word ---------------------------
// bs_collapse's inner do/while is a data-dependent branch, mispredicted once
// per group. When groups are short (unclustered data) that is a branch per
// element and worse than V1. This form peels the common "group of one" case so
// the unclustered path costs a predictable branch, and only pays the loop for
// genuine groups.
uint64_t bs_collapse_peel(const BitmapView& b, const ListView& s) {
    uint64_t c = 0;
    uint32_t i = 0;
    while (i < s.n) {
        const uint32_t v0 = s.v[i];
        const uint32_t w  = v0 >> 6;
        if (i + 1 >= s.n || (s.v[i + 1] >> 6) != w) {      // group of one
            c += (b.w[w] >> (v0 & 63)) & 1u;
            ++i;
            continue;
        }
        uint64_t mask = uint64_t(1) << (v0 & 63);
        ++i;
        do {
            mask |= uint64_t(1) << (s.v[i] & 63);
            ++i;
        } while (i < s.n && (s.v[i] >> 6) == w);
        c += STORM_POPCOUNT(b.w[w] & mask);
    }
    return c;
}

// --- V5: D4, software-pipelined with explicit prefetch ----------------------
// storm.cpp carries commented-out __builtin_prefetch calls in exactly this
// kernel -- someone started this in 2019 and stopped (RESEARCH_PLAN.md 4, D4).
// Finished here. The list is sorted, so the access stream is monotonic and the
// hardware prefetcher should already do well; the question this variant answers
// is whether the strides are too large and irregular for it, which is precisely
// the sparse-unclustered regime.
template <int DIST>
uint64_t bs_prefetch(const BitmapView& b, const ListView& s) {
    uint64_t acc0 = 0, acc1 = 0;
    uint32_t i = 0;
    for (; i + 2 <= s.n; i += 2) {
        // Guard BOTH lookahead reads. `i + DIST < s.n` admits
        // i + DIST == s.n - 1, and the second prefetch then reads s.v[s.n] --
        // a heap-buffer-overflow that ASan caught and 2.6 M correctness checks
        // did not, because the value is only used to compute a prefetch address
        // and never changes a result.
        if (i + DIST + 1 < s.n) {
            __builtin_prefetch(&b.w[s.v[i + DIST] >> 6], 0, 1);
            __builtin_prefetch(&b.w[s.v[i + DIST + 1] >> 6], 0, 1);
        }
        acc0 += (b.w[s.v[i + 0] >> 6] >> (s.v[i + 0] & 63)) & 1u;
        acc1 += (b.w[s.v[i + 1] >> 6] >> (s.v[i + 1] & 63)) & 1u;
    }
    uint64_t c = acc0 + acc1;
    for (; i < s.n; ++i) c += (b.w[s.v[i] >> 6] >> (s.v[i] & 63)) & 1u;
    return c;
}

// --- V8: explicit named accumulators ---------------------------------------
// bs_ilp holds its accumulators in an ARRAY, and the baseline showed ilp2 and
// ilp4 LOSING to the single-accumulator `shift` form (0.76x and 0.79x on the
// 1/i corpus). An array indexed by the inner loop variable is only kept in
// registers if the compiler fully unrolls and promotes it; when it does not,
// every accumulation becomes a stack load/store and the extra chains cost more
// than they buy. Naming them removes the question.
uint64_t bs_ilp4x(const BitmapView& b, const ListView& s) {
    uint64_t a0 = 0, a1 = 0, a2 = 0, a3 = 0;
    uint32_t i = 0;
    for (; i + 4 <= s.n; i += 4) {
        const uint32_t v0 = s.v[i], v1 = s.v[i + 1], v2 = s.v[i + 2], v3 = s.v[i + 3];
        a0 += (b.w[v0 >> 6] >> (v0 & 63)) & 1u;
        a1 += (b.w[v1 >> 6] >> (v1 & 63)) & 1u;
        a2 += (b.w[v2 >> 6] >> (v2 & 63)) & 1u;
        a3 += (b.w[v3 >> 6] >> (v3 & 63)) & 1u;
    }
    uint64_t c = a0 + a1 + a2 + a3;
    for (; i < s.n; ++i) c += (b.w[s.v[i] >> 6] >> (s.v[i] & 63)) & 1u;
    return c;
}

uint64_t bs_ilp8x(const BitmapView& b, const ListView& s) {
    uint64_t a0 = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0, a5 = 0, a6 = 0, a7 = 0;
    uint32_t i = 0;
    for (; i + 8 <= s.n; i += 8) {
        const uint32_t v0 = s.v[i],     v1 = s.v[i + 1], v2 = s.v[i + 2], v3 = s.v[i + 3];
        const uint32_t v4 = s.v[i + 4], v5 = s.v[i + 5], v6 = s.v[i + 6], v7 = s.v[i + 7];
        a0 += (b.w[v0 >> 6] >> (v0 & 63)) & 1u;
        a1 += (b.w[v1 >> 6] >> (v1 & 63)) & 1u;
        a2 += (b.w[v2 >> 6] >> (v2 & 63)) & 1u;
        a3 += (b.w[v3 >> 6] >> (v3 & 63)) & 1u;
        a4 += (b.w[v4 >> 6] >> (v4 & 63)) & 1u;
        a5 += (b.w[v5 >> 6] >> (v5 & 63)) & 1u;
        a6 += (b.w[v6 >> 6] >> (v6 & 63)) & 1u;
        a7 += (b.w[v7 >> 6] >> (v7 & 63)) & 1u;
    }
    uint64_t c = (a0 + a1) + (a2 + a3) + (a4 + a5) + (a6 + a7);
    for (; i < s.n; ++i) c += (b.w[s.v[i] >> 6] >> (s.v[i] & 63)) & 1u;
    return c;
}

// --- V9: two positions per list load ---------------------------------------
// The per-element work is TWO loads -- one sequential from the list, one
// scattered into the bitmap -- plus a shift and a mask. Firestorm retires 3
// loads/cycle (applecpu: LDR recip TP 0.333, u8-10), so at 2 loads/element the
// floor is 0.67 cycles/element and the measured cost is ~2. Loads are the
// resource worth attacking first.
//
// The list is a sorted uint32 array, so two consecutive positions are one
// aligned 64-bit load. That cuts list traffic in half and takes the pair to 1.5
// loads/element. Alignment is guaranteed: `list` is an avec with 64-byte
// alignment and the loop only reads at even i.
//
// Deliberately uses memcpy rather than a uint64_t* cast -- the cast is a strict
// aliasing violation and clang is entitled to reorder around it. memcpy of 8
// bytes compiles to the same single LDR.
// --- V10: two independent cursors -------------------------------------------
// ilp8x already gives the scheduler 8 independent chains, but they all walk ONE
// sequential list stream, so every bitmap load is issued from the same point in
// the instruction window. Splitting the list in half and walking both halves
// concurrently creates two independent access streams: two hardware prefetch
// streams instead of one, and twice the span between the oldest and newest
// outstanding bitmap load.
//
// The competing effect is that two streams halve the per-stream prefetch depth
// and double the list's own L1 footprint, so this is genuinely a question for
// measurement rather than one with an obvious answer.
uint64_t bs_split2(const BitmapView& b, const ListView& s) {
    const uint32_t half = s.n / 2;
    uint64_t a0 = 0, a1 = 0, a2 = 0, a3 = 0;
    uint32_t i = 0, j = half;
    for (; i + 2 <= half && j + 2 <= s.n; i += 2, j += 2) {
        const uint32_t v0 = s.v[i], v1 = s.v[i + 1];
        const uint32_t v2 = s.v[j], v3 = s.v[j + 1];
        a0 += (b.w[v0 >> 6] >> (v0 & 63)) & 1u;
        a1 += (b.w[v1 >> 6] >> (v1 & 63)) & 1u;
        a2 += (b.w[v2 >> 6] >> (v2 & 63)) & 1u;
        a3 += (b.w[v3 >> 6] >> (v3 & 63)) & 1u;
    }
    uint64_t c = (a0 + a1) + (a2 + a3);
    for (; i < half; ++i) c += (b.w[s.v[i] >> 6] >> (s.v[i] & 63)) & 1u;
    for (; j < s.n; ++j)  c += (b.w[s.v[j] >> 6] >> (s.v[j] & 63)) & 1u;
    return c;
}

uint64_t bs_pack2(const BitmapView& b, const ListView& s) {
    uint64_t a0 = 0, a1 = 0, a2 = 0, a3 = 0;
    uint32_t i = 0;
    for (; i + 4 <= s.n; i += 4) {
        uint64_t p01, p23;
        __builtin_memcpy(&p01, s.v + i,     8);
        __builtin_memcpy(&p23, s.v + i + 2, 8);
        const uint32_t v0 = (uint32_t)p01,        v1 = (uint32_t)(p01 >> 32);
        const uint32_t v2 = (uint32_t)p23,        v3 = (uint32_t)(p23 >> 32);
        a0 += (b.w[v0 >> 6] >> (v0 & 63)) & 1u;
        a1 += (b.w[v1 >> 6] >> (v1 & 63)) & 1u;
        a2 += (b.w[v2 >> 6] >> (v2 & 63)) & 1u;
        a3 += (b.w[v3 >> 6] >> (v3 & 63)) & 1u;
    }
    uint64_t c = (a0 + a1) + (a2 + a3);
    for (; i < s.n; ++i) c += (b.w[s.v[i] >> 6] >> (s.v[i] & 63)) & 1u;
    return c;
}

#if STORM_CELL_NEON
// --- V6: NEON-assisted collapse --------------------------------------------
// NEON has no gather, so the bitmap loads stay scalar. What CAN be vectorized
// is the index arithmetic and the group detection: 4 positions per iteration
// give word indices with one shift and a same-word predicate with one compare,
// replacing per-element scalar shifts and branches.
//
// The loads remain the serial part, so the ceiling here is set by how much of
// the per-element ALU work is removable, not by the SIMD width. That is worth
// knowing either way -- it is the direct answer to "why not just vectorize
// B x S like the list x list papers do".
uint64_t bs_neon_idx(const BitmapView& b, const ListView& s) {
    uint64_t c = 0;
    uint32_t i = 0;
    uint32_t idx[4], bit[4];
    for (; i + 4 <= s.n; i += 4) {
        const uint32x4_t p = vld1q_u32(s.v + i);
        vst1q_u32(idx, vshrq_n_u32(p, 6));
        vst1q_u32(bit, vandq_u32(p, vdupq_n_u32(63)));
        // Same-word groups within the quad: fold masks before touching memory.
        uint64_t m0 = uint64_t(1) << bit[0];
        uint32_t w0 = idx[0];
        for (int k = 1; k < 4; ++k) {
            if (idx[k] == w0) {
                m0 |= uint64_t(1) << bit[k];
            } else {
                c += STORM_POPCOUNT(b.w[w0] & m0);
                w0 = idx[k];
                m0 = uint64_t(1) << bit[k];
            }
        }
        c += STORM_POPCOUNT(b.w[w0] & m0);
    }
    for (; i < s.n; ++i) c += (b.w[s.v[i] >> 6] >> (s.v[i] & 63)) & 1u;
    return c;
}
#endif

// --- V12: rank-gated block skipping ------------------------------------------
// The work-reduction form of B x S, and the one the campaign's F5 finding says
// should be tried before any more vectorization.
//
// The list is sorted, so its elements arrive grouped by 512-bit rank block. If
// the dense side's block is entirely zero -- one O(1) comparison of two rank
// counters, no bitmap touch -- then EVERY list element in that block
// contributes nothing and the whole group is skipped without a single probe.
//
// This is the same mechanism as bb_neon_rankskip and br_rank: pay O(1) to prove
// a region is empty instead of O(region) to confirm it. Under a 1/i spectrum
// the dense side is usually far from full, so the fraction of blocks that are
// empty is exactly the fraction of probes avoided.
//
// The cost when it does not fire is one extra rank load per block boundary,
// amortized over the elements in that block -- so the risk is concentrated on
// unclustered lists, where consecutive elements rarely share a block.
uint64_t bs_rankskip(const BitmapView& b, const ListView& s) {
    if (b.rank == nullptr) return bs_ilp8x(b, s);
    const uint32_t nblk = (b.nw + BitmapView::RANK_STRIDE - 1) / BitmapView::RANK_STRIDE;
    uint64_t c = 0;
    uint32_t i = 0;
    while (i < s.n) {
        const uint32_t blk = s.v[i] >> 9;              // 512 bits per rank block
        // Span of the list that falls in this block.
        uint32_t j = i;
        while (j < s.n && (s.v[j] >> 9) == blk) ++j;
        if (blk < nblk && !rank_block_empty(b, blk)) {
            uint64_t a0 = 0, a1 = 0;
            uint32_t k = i;
            for (; k + 2 <= j; k += 2) {
                const uint32_t v0 = s.v[k], v1 = s.v[k + 1];
                a0 += (b.w[v0 >> 6] >> (v0 & 63)) & 1u;
                a1 += (b.w[v1 >> 6] >> (v1 & 63)) & 1u;
            }
            c += a0 + a1;
            for (; k < j; ++k) c += (b.w[s.v[k] >> 6] >> (s.v[k] & 63)) & 1u;
        }
        i = j;
    }
    return c;
}

// --- V11: length-adaptive ----------------------------------------------------
// No single B x S variant wins everywhere, and the split is systematic rather
// than noise. On the dense corpus (|S| ~ 23,000, bitmap resident in L1) the
// plain `shift` loop measures 1.59x while ilp8 measures 0.81x; on the sparse
// corpora the order reverses.
//
// The reason is that the multi-chain forms buy memory-level parallelism, and
// MLP is only worth register pressure when the bitmap loads actually miss. Once
// the bitmap fits L1 the loads are ~4 cycles and the extra chains just cost
// registers and loop overhead. So the switch is on the DENSE side's footprint,
// not on the list length -- which is the metadata already available for free.
//
// The threshold is where the bitmap stops fitting L1d (64 kB on this host).
// Another M4-calibrated constant that belongs in the cost model (M4); named and
// measured here rather than buried.
constexpr uint32_t kBsL1Words = 8192;      // 64 kB / 8 bytes

uint64_t bs_adaptive(const BitmapView& b, const ListView& s) {
    return (b.nw <= kBsL1Words / 2) ? bs_shift(b, s) : bs_ilp8x(b, s);
}

// --- V13: 16 accumulator chains ----------------------------------------------
// 8 chains beat 4 and 4 beat 1; the series has not obviously saturated. If 16
// also helps, the kernel is still latency bound and more lookahead is the lever.
// If it does not, 8 is the plateau and the remaining gap is the scattered load
// itself -- which is the answer that closes the cell.
uint64_t bs_ilp16x(const BitmapView& b, const ListView& s) {
    uint64_t a[16] = {0};
    uint32_t i = 0;
    for (; i + 16 <= s.n; i += 16) {
#define STORM_BS_P(k) { const uint32_t v = s.v[i + k]; a[k] += (b.w[v >> 6] >> (v & 63)) & 1u; }
        STORM_BS_P(0)  STORM_BS_P(1)  STORM_BS_P(2)  STORM_BS_P(3)
        STORM_BS_P(4)  STORM_BS_P(5)  STORM_BS_P(6)  STORM_BS_P(7)
        STORM_BS_P(8)  STORM_BS_P(9)  STORM_BS_P(10) STORM_BS_P(11)
        STORM_BS_P(12) STORM_BS_P(13) STORM_BS_P(14) STORM_BS_P(15)
#undef STORM_BS_P
    }
    uint64_t c = 0;
    for (int k = 0; k < 16; ++k) c += a[k];
    for (; i < s.n; ++i) c += (b.w[s.v[i] >> 6] >> (s.v[i] & 63)) & 1u;
    return c;
}

// --- V14: byte-granular skip via a nonzero-word summary ----------------------
// bs_rankskip skips at 512-bit granularity. Most of the win, though, is in
// whether the single WORD a probe lands on is zero -- and the rank index can
// answer that for a whole block boundary but not for one word.
//
// This variant instead exploits the dense side's own structure: it reads the
// word once and reuses it for every list element in that word, so a group of
// same-word probes costs one load rather than one per element. Unlike
// bs_collapse it does NOT build a mask or branch per group -- it just caches
// the last word and its index, which is a predictable compare against a value
// already in a register.
uint64_t bs_cacheword(const BitmapView& b, const ListView& s) {
    uint64_t c = 0;
    uint32_t last = UINT32_MAX;
    uint64_t word = 0;
    for (uint32_t i = 0; i < s.n; ++i) {
        const uint32_t v = s.v[i], wi = v >> 6;
        if (wi != last) { last = wi; word = b.w[wi]; }
        c += (word >> (v & 63)) & 1u;
    }
    return c;
}

// --- V15: 16 chains with same-word reuse -------------------------------------
// ilp16x won on two corpora and cacheword lost on all of them, but they attack
// different costs: chains hide the scattered load's latency, word reuse removes
// the load entirely when consecutive probes share a word. Combining them tests
// whether the two are additive or whether the compare that enables reuse costs
// more than the load it saves.
uint64_t bs_ilp_cache(const BitmapView& b, const ListView& s) {
    uint64_t a0 = 0, a1 = 0, a2 = 0, a3 = 0;
    uint32_t last0 = UINT32_MAX, last1 = UINT32_MAX;
    uint64_t w0 = 0, w1 = 0;
    uint32_t i = 0;
    for (; i + 4 <= s.n; i += 4) {
        const uint32_t v0 = s.v[i], v1 = s.v[i + 1], v2 = s.v[i + 2], v3 = s.v[i + 3];
        const uint32_t i0 = v0 >> 6, i1 = v1 >> 6, i2 = v2 >> 6, i3 = v3 >> 6;
        if (i0 != last0) { last0 = i0; w0 = b.w[i0]; }
        a0 += (w0 >> (v0 & 63)) & 1u;
        if (i1 != last0) { last0 = i1; w0 = b.w[i1]; }
        a1 += (w0 >> (v1 & 63)) & 1u;
        if (i2 != last1) { last1 = i2; w1 = b.w[i2]; }
        a2 += (w1 >> (v2 & 63)) & 1u;
        if (i3 != last1) { last1 = i3; w1 = b.w[i3]; }
        a3 += (w1 >> (v3 & 63)) & 1u;
    }
    uint64_t c = (a0 + a1) + (a2 + a3);
    for (; i < s.n; ++i) c += (b.w[s.v[i] >> 6] >> (s.v[i] & 63)) & 1u;
    return c;
}

// --- V16: deep prefetch ------------------------------------------------------
// bs_prefetch at distances 16 and 32 both lost. On a 2-load-per-element loop
// with ~4-cycle L1 hits, 16 elements of lookahead is only ~30 cycles -- far
// short of an L2 or SLC miss. If prefetch is going to help at all it needs a
// distance matched to the miss latency, so this probes 128.
uint64_t bs_prefetch_deep(const BitmapView& b, const ListView& s) {
    uint64_t a0 = 0, a1 = 0, a2 = 0, a3 = 0;
    uint32_t i = 0;
    constexpr uint32_t D = 128;
    for (; i + 4 <= s.n; i += 4) {
        if (i + D + 4 <= s.n) {
            __builtin_prefetch(&b.w[s.v[i + D] >> 6], 0, 0);
            __builtin_prefetch(&b.w[s.v[i + D + 2] >> 6], 0, 0);
        }
        const uint32_t v0 = s.v[i], v1 = s.v[i + 1], v2 = s.v[i + 2], v3 = s.v[i + 3];
        a0 += (b.w[v0 >> 6] >> (v0 & 63)) & 1u;
        a1 += (b.w[v1 >> 6] >> (v1 & 63)) & 1u;
        a2 += (b.w[v2 >> 6] >> (v2 & 63)) & 1u;
        a3 += (b.w[v3 >> 6] >> (v3 & 63)) & 1u;
    }
    uint64_t c = (a0 + a1) + (a2 + a3);
    for (; i < s.n; ++i) c += (b.w[s.v[i] >> 6] >> (s.v[i] & 63)) & 1u;
    return c;
}

// --- V17: zone-map gating ----------------------------------------------------
// bs_rankskip gates on the rank index: two 64-bit counters per 512-bit block.
// The zone map answers the same question -- "is this bin empty?" -- with ONE
// BIT, so a single occ word covers 64 bins = 32,768 bits of universe. The index
// traffic drops by ~128x and the whole zone map of a row stays in L1 even when
// its bitmap does not.
//
// Same structure as bs_rankskip otherwise, so the difference measured between
// them is purely the cost of consulting the summary.
uint64_t bs_occ(const BitmapView& b, const ListView& s) {
    if (b.occ == nullptr) return bs_ilp8x(b, s);
    uint64_t c = 0;
    uint32_t i = 0;
    while (i < s.n) {
        const uint32_t bin = s.v[i] >> 9;              // 8 words = 512 bits per bin
        uint32_t j = i;
        while (j < s.n && (s.v[j] >> 9) == bin) ++j;
        const uint32_t ow = bin >> 6;
        if (ow < b.n_occ && ((b.occ[ow] >> (bin & 63)) & 1u)) {
            uint64_t a0 = 0, a1 = 0;
            uint32_t k = i;
            for (; k + 2 <= j; k += 2) {
                const uint32_t v0 = s.v[k], v1 = s.v[k + 1];
                a0 += (b.w[v0 >> 6] >> (v0 & 63)) & 1u;
                a1 += (b.w[v1 >> 6] >> (v1 & 63)) & 1u;
            }
            c += a0 + a1;
            for (; k < j; ++k) c += (b.w[s.v[k] >> 6] >> (s.v[k] & 63)) & 1u;
        }
        i = j;
    }
    return c;
}

// --- V7: the inflate-to-bitmap fallback, as a LABELLED baseline ------------
// AGENTS.md standing rule 3: never inflate a sparse side to a bitmap to reuse
// the B x B kernel -- it recovers zero of the available saving. It appears here
// for one reason only: beating it is claim P3, and a claim needs the thing it
// beats to be present and measured, not asserted.
/* Grow-only scratch, deliberately bounded.
 *
 * This is the labelled inflate-to-bitmap baseline (standing rule 3), so it is
 * never on a hot path -- but it is thread_local and was never released. At the
 * 10^7-bit universes PROBLEM_STATEMENT.md 2 motivates that is 1.25 MB retained
 * per thread that ever touches this variant, times every thread in a pool. The
 * cap turns an unbounded retention into a fallback that degrades loudly. */
constexpr size_t kInflateMaxWords = 1u << 20;   // 8 MB
thread_local std::vector<uint64_t> g_inflate;

uint64_t bs_inflate(const BitmapView& b, const ListView& s) {
    if (b.nw > kInflateMaxWords) return 0;      // refuse rather than retain
    if (g_inflate.size() < b.nw) g_inflate.assign(b.nw, 0);
    for (uint32_t i = 0; i < s.n; ++i)
        g_inflate[s.v[i] >> 6] |= uint64_t(1) << (s.v[i] & 63);

    uint64_t c = 0;
    for (uint32_t k = 0; k < b.nw; ++k) c += STORM_POPCOUNT(b.w[k] & g_inflate[k]);

    for (uint32_t i = 0; i < s.n; ++i) g_inflate[s.v[i] >> 6] = 0;   // clear by list, not memset
    return c;
}

const Variant<fn_bs> kBS[] = {
    {"scalar",         bs_scalar,        "reference: STORM_bitmap_x_list, one test per element"},
    {"shift",          bs_shift,         "branchless (w >> bit) & 1"},
    {"ilp2",           bs_ilp<2>,        "2 independent accumulator chains"},
    {"ilp4",           bs_ilp<4>,        "4 chains"},
    {"ilp8",           bs_ilp<8>,        "8 chains"},
    {"ilp12",          bs_ilp<12>,       "12 chains -- between the tested 8 and 16"},
    {"prefetch64",     bs_prefetch<64>,  "D4: prefetch distance 64"},
    {"ilp4x",          bs_ilp4x,         "4 chains, named accumulators (no array)"},
    {"ilp8x",          bs_ilp8x,         "8 chains, named accumulators"},
    {"pack2",          bs_pack2,         "two positions per 64-bit list load, 4 chains"},
    {"split2",         bs_split2,        "two independent cursors over halves of the list"},
    {"adaptive",       bs_adaptive,      "shift when the bitmap is L1-resident, ilp8x when not"},
    {"ilp16x",         bs_ilp16x,        "16 chains -- has the ILP series saturated?"},
    {"cacheword",      bs_cacheword,     "reuse the last loaded word across same-word probes"},
    {"ilp_cache",      bs_ilp_cache,     "16-chain style ILP plus same-word reuse"},
    {"prefetch_deep",  bs_prefetch_deep, "prefetch 128 elements ahead, matched to a miss"},
    {"occ",            bs_occ,           "zone map gates each 512-bit bin -- 1 bit per bin", true},
    {"rankskip",       bs_rankskip,      "skip list groups whose dense-side rank block is empty", true},
    {"collapse",       bs_collapse,      "D2: fold same-word groups into one mask"},
    {"collapse_peel",  bs_collapse_peel, "D2 with the group-of-one case peeled out"},
    {"prefetch16",     bs_prefetch<16>,  "D4: software pipelined, prefetch distance 16"},
    {"prefetch32",     bs_prefetch<32>,  "D4: prefetch distance 32"},
#if STORM_CELL_NEON
    {"neon_idx",       bs_neon_idx,      "NEON index arithmetic + quad-local collapse"},
#endif
    {"inflate",        bs_inflate,       "BASELINE ONLY: materialize S as a bitmap, run B x B",
                                          false, true},
};

} // namespace

VariantList<fn_bs> cell_bs() { return {kBS, sizeof(kBS) / sizeof(kBS[0])}; }

} // namespace storm
