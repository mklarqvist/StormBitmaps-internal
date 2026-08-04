/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist
 * Licensed under the Apache License, Version 2.0.
 *
 * Cell B x B — dense bitmap against dense bitmap. Theta(m) regardless of
 * density; this is the fixed cost the project exists to avoid paying
 * (PROBLEM_STATEMENT.md 2). It is P2 priority and lives here as the reference
 * everything else is measured against, plus the honest ceiling for the ~1% of
 * pairs on skewed data where both sides really are dense.
 *
 * --- Preflight (AGENTS.md evidence ladder) ---------------------------------
 * Intrinsics, from the local Arm corpus (tier 2 -- sourced):
 *   vandq_u8    -> AND    Vd.16B,Vn.16B,Vm.16B   v7/A32/A64   (logical/)
 *   vcntq_u8    -> CNT    Vd.16B,Vn.16B          v7/A32/A64   (bit-manipulation/)
 *   vpadalq_u8  -> UADALP Vd.8H,Vn.16B           v7/A32/A64   (vector-arithmetic/)
 *   vpadalq_u16 -> UADALP Vd.4S,Vn.8H            v7/A32/A64
 *   vpadalq_u32 -> UADALP Vd.2D,Vn.4S            v7/A32/A64
 *   vaddvq_u64  -> ADDP   Dd,Vn.2D               A64
 * All baseline Advanced SIMD; no extension gate beyond AArch64, so no +flag is
 * required and none is assumed.
 *
 * Firestorm resources, from applecpu (tier 2 -- sourced, and Firestorm is NOT
 * this host, so these are a hypothesis to be measured, not a claim):
 *   AND/CNT/UADALP/ADDP   LAT 2-3, recip TP 0.25  -> 4 SIMD pipes (u11-14)
 *   LDR (Q)               LAT <=9, recip TP 0.333 -> 3 load pipes (u8-10)
 *
 * The derived prediction that shapes this file: the straightforward kernel
 * issues 1 AND + 1 CNT + 1 accumulate per 16 bytes = 3 SIMD ops, against only
 * 2 loads. At 4 SIMD/cycle and 3 loads/cycle that is 0.75 cycles per 16 bytes
 * from the SIMD pipes versus 0.667 from the load pipes, so the NEON kernel is
 * predicted SIMD-ISSUE bound, not load bound.
 *
 * That is the opposite of RESEARCH_PLAN.md 3.1, which derives a load-bound
 * kernel from the AVX-512 port structure (VPOPCNTQ alone on p5). The difference
 * is real and not a detail: on AVX-512 the popcount has a dedicated port and
 * the AND/ADD go elsewhere, so cutting loads via register blocking pays. On
 * NEON all three ops compete for the same four pipes, so register blocking cuts
 * a resource that was not binding. Prediction: MRxNR register blocking buys
 * little to nothing for B x B on this ISA. Measured in the harness.
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

// --- V0: the floor ---------------------------------------------------------
uint64_t bb_scalar(const BitmapView& a, const BitmapView& b) {
    uint64_t c = 0;
    for (uint32_t i = 0; i < a.nw; ++i) c += STORM_POPCOUNT(a.w[i] & b.w[i]);
    return c;
}

// --- V1: what the repo shipped in 2019 -------------------------------------
// libalgebra's 4x-unrolled scalar. On AArch64 libalgebra has no NEON path at
// all (LANDSCAPE.md 8.2), so this is what StormBitmaps actually ran on this
// host before today.
uint64_t bb_scalar_u4(const BitmapView& a, const BitmapView& b) {
    return STORM_intersect_count_unrolled(a.w, b.w, a.nw);
}

#if STORM_CELL_NEON

// --- V2: the straightforward NEON kernel -----------------------------------
// 1 AND + 1 CNT + 1 UADALP per 16 bytes. u16 lanes hold 65535 and each CNT lane
// contributes at most 8, so the u16 accumulator is safe for 8191 iterations;
// at 16 bytes each that is 128 kB, more than any m we benchmark, so no
// intermediate flush is needed.
uint64_t bb_neon(const BitmapView& a, const BitmapView& b) {
    const uint8_t* pa = (const uint8_t*)a.w;
    const uint8_t* pb = (const uint8_t*)b.w;
    const uint32_t nb = a.nw * 8;

    uint16x8_t acc = vdupq_n_u16(0);
    uint32_t i = 0;
    for (; i + 16 <= nb; i += 16) {
        const uint8x16_t x = vandq_u8(vld1q_u8(pa + i), vld1q_u8(pb + i));
        acc = vpadalq_u8(acc, vcntq_u8(x));
    }
    uint64_t c = vaddvq_u64(vpadalq_u32(vdupq_n_u64(0), vpadalq_u16(vdupq_n_u32(0), acc)));
    for (uint32_t k = i / 8; k < a.nw; ++k) c += STORM_POPCOUNT(a.w[k] & b.w[k]);
    return c;
}

// --- V3/V4/V5: unrolled, multiple accumulators -----------------------------
// UADALP has latency 3 on Firestorm and the four pipes retire 4/cycle, so a
// single accumulator chain caps at one UADALP per 3 cycles -- a third of peak.
// U independent accumulators issue 3U ops per unrolled iteration = 0.75U
// cycles, and the same accumulator is revisited every 0.75U cycles, so U >= 4
// is needed to cover the latency-3 chain. U = 2 should be latency bound, U = 4
// marginal, U = 8 comfortable. Whether that prediction survives on M4 is the
// measurement.
template <int U>
uint64_t bb_neon_unroll(const BitmapView& a, const BitmapView& b) {
    const uint8_t* pa = (const uint8_t*)a.w;
    const uint8_t* pb = (const uint8_t*)b.w;
    const uint32_t nb = a.nw * 8;

    uint16x8_t acc[U];
    for (int u = 0; u < U; ++u) acc[u] = vdupq_n_u16(0);

    uint32_t i = 0;
    for (; i + 16 * U <= nb; i += 16 * U) {
        for (int u = 0; u < U; ++u) {
            const uint8x16_t x = vandq_u8(vld1q_u8(pa + i + 16 * u),
                                          vld1q_u8(pb + i + 16 * u));
            acc[u] = vpadalq_u8(acc[u], vcntq_u8(x));
        }
    }
    uint32x4_t s32 = vdupq_n_u32(0);
    for (int u = 0; u < U; ++u) s32 = vpadalq_u16(s32, acc[u]);

    uint16x8_t tail = vdupq_n_u16(0);
    for (; i + 16 <= nb; i += 16) {
        const uint8x16_t x = vandq_u8(vld1q_u8(pa + i), vld1q_u8(pb + i));
        tail = vpadalq_u8(tail, vcntq_u8(x));
    }
    s32 = vpadalq_u16(s32, tail);

    uint64_t c = vaddvq_u64(vpadalq_u32(vdupq_n_u64(0), s32));
    for (uint32_t k = i / 8; k < a.nw; ++k) c += STORM_POPCOUNT(a.w[k] & b.w[k]);
    return c;
}

// --- V6: u8 accumulation, deferred widening --------------------------------
// Trades UADALP (latency 3) for ADD (latency 2) in the inner chain and pays one
// widening UADALP every 31 iterations. Same 3 ops per 16 bytes, shorter
// recurrence -- so if V3 is latency bound rather than issue bound, this should
// separate them.
template <int U>
uint64_t bb_neon_u8acc(const BitmapView& a, const BitmapView& b) {
    const uint8_t* pa = (const uint8_t*)a.w;
    const uint8_t* pb = (const uint8_t*)b.w;
    const uint32_t nb = a.nw * 8;

    uint16x8_t wide[U];
    for (int u = 0; u < U; ++u) wide[u] = vdupq_n_u16(0);

    uint32_t i = 0;
    // Each CNT lane is at most 8, so a u8 lane saturates after 31 adds.
    const uint32_t chunk = 31u * 16u * U;
    while (i + chunk <= nb) {
        uint8x16_t acc[U];
        for (int u = 0; u < U; ++u) acc[u] = vdupq_n_u8(0);
        const uint32_t stop = i + chunk;
        for (; i < stop; i += 16 * U) {
            for (int u = 0; u < U; ++u) {
                const uint8x16_t x = vandq_u8(vld1q_u8(pa + i + 16 * u),
                                              vld1q_u8(pb + i + 16 * u));
                acc[u] = vaddq_u8(acc[u], vcntq_u8(x));
            }
        }
        for (int u = 0; u < U; ++u) wide[u] = vpadalq_u8(wide[u], acc[u]);
    }
    // Remainder: at most 31*16*U bytes, folded through a fresh u8 accumulator
    // in blocks of 31 so it can never overflow.
    while (i + 16 <= nb) {
        uint8x16_t acc0 = vdupq_n_u8(0);
        for (int r = 0; r < 31 && i + 16 <= nb; ++r, i += 16) {
            const uint8x16_t x = vandq_u8(vld1q_u8(pa + i), vld1q_u8(pb + i));
            acc0 = vaddq_u8(acc0, vcntq_u8(x));
        }
        wide[0] = vpadalq_u8(wide[0], acc0);
    }

    uint32x4_t s32 = vdupq_n_u32(0);
    for (int u = 0; u < U; ++u) s32 = vpadalq_u16(s32, wide[u]);
    uint64_t c = vaddvq_u64(vpadalq_u32(vdupq_n_u64(0), s32));
    for (uint32_t k = i / 8; k < a.nw; ++k) c += STORM_POPCOUNT(a.w[k] & b.w[k]);
    return c;
}

// --- V7: Harley-Seal, as a labelled negative control -----------------------
// RESEARCH_PLAN.md 3.3 argues CSA is a pessimization once a native vector
// popcount exists, and derives it for AVX-512. The same arithmetic on NEON is
// even more lopsided: a CSA is 5 ops (2 XOR, 2 AND, 1 OR) and it buys back CNTs
// that cost exactly 1 op each at 4/cycle. Reducing 16 vectors costs 15 CSAs =
// 75 ops to remove ~15 CNT+ADD pairs = 30 ops. Predicted ~2x LOSS.
//
// It is implemented anyway because the plan says to measure the claim rather
// than assert it, and because a negative result with a number attached is worth
// more than a paragraph of reasoning.
static inline uint8x16_t csa(uint8x16_t& h, uint8x16_t a, uint8x16_t b, uint8x16_t c) {
    const uint8x16_t u = veorq_u8(a, b);
    h = vorrq_u8(vandq_u8(a, b), vandq_u8(u, c));
    return veorq_u8(u, c);
}

uint64_t bb_neon_harley_seal(const BitmapView& a, const BitmapView& b) {
    const uint8_t* pa = (const uint8_t*)a.w;
    const uint8_t* pb = (const uint8_t*)b.w;
    const uint32_t nb = a.nw * 8;

    uint16x8_t total = vdupq_n_u16(0);
    uint8x16_t ones = vdupq_n_u8(0), twos = vdupq_n_u8(0);
    uint8x16_t fours = vdupq_n_u8(0), eights = vdupq_n_u8(0);
    uint8x16_t twosA, twosB, foursA, foursB, eightsA, eightsB, sixteens;

    auto ld = [&](uint32_t off) {
        return vandq_u8(vld1q_u8(pa + off), vld1q_u8(pb + off));
    };

    uint32_t i = 0;
    // 16 vectors per block, 15 CSAs, reducing to one weight-16 accumulator.
    // `total` counts sixteens only, unweighted: each block adds at most 8 to a
    // u16 lane, so it is safe for 8191 blocks (2 MB) with no intermediate
    // flush. Applying the x16 weight at the end rather than with a vshlq_n_u8
    // inside the loop is what keeps that headroom -- the shifted form saturates
    // 32x sooner and is the kind of bound that is easy to write and easy to
    // exceed silently.
    for (; i + 256 <= nb; i += 256) {
        ones = csa(twosA,   ones,  ld(i +   0), ld(i +  16));
        ones = csa(twosB,   ones,  ld(i +  32), ld(i +  48));
        twos = csa(foursA,  twos,  twosA, twosB);
        ones = csa(twosA,   ones,  ld(i +  64), ld(i +  80));
        ones = csa(twosB,   ones,  ld(i +  96), ld(i + 112));
        twos = csa(foursB,  twos,  twosA, twosB);
        fours= csa(eightsA, fours, foursA, foursB);

        ones = csa(twosA,   ones,  ld(i + 128), ld(i + 144));
        ones = csa(twosB,   ones,  ld(i + 160), ld(i + 176));
        twos = csa(foursA,  twos,  twosA, twosB);
        ones = csa(twosA,   ones,  ld(i + 192), ld(i + 208));
        ones = csa(twosB,   ones,  ld(i + 224), ld(i + 240));
        twos = csa(foursB,  twos,  twosA, twosB);
        fours= csa(eightsB, fours, foursA, foursB);
        eights=csa(sixteens,eights,eightsA, eightsB);

        total = vpadalq_u8(total, vcntq_u8(sixteens));
    }
    uint32x4_t s32 = vpadalq_u16(vdupq_n_u32(0), total);
    uint64_t c = 16 * vaddvq_u64(vpadalq_u32(vdupq_n_u64(0), s32));

    // Flush the partial CSA state with its weights.
    uint16x8_t t8 = vpadalq_u8(vdupq_n_u16(0), vcntq_u8(eights));
    uint16x8_t t4 = vpadalq_u8(vdupq_n_u16(0), vcntq_u8(fours));
    uint16x8_t t2 = vpadalq_u8(vdupq_n_u16(0), vcntq_u8(twos));
    uint16x8_t t1 = vpadalq_u8(vdupq_n_u16(0), vcntq_u8(ones));
    auto red16 = [](uint16x8_t v) {
        return vaddvq_u64(vpadalq_u32(vdupq_n_u64(0), vpadalq_u16(vdupq_n_u32(0), v)));
    };
    c += 8 * red16(t8) + 4 * red16(t4) + 2 * red16(t2) + red16(t1);

    for (uint32_t k = i / 8; k < a.nw; ++k) c += STORM_POPCOUNT(a.w[k] & b.w[k]);
    return c;
}

// --- V7b: paired loads -------------------------------------------------------
// The kernel is predicted SIMD-issue bound (3 ops per 16 bytes against 2 loads),
// so cutting loads should buy nothing -- and that prediction deserves a direct
// test rather than an argument. vld1q_u8_x2 issues ONE LD1 for two Q registers
// (applecpu: "LD1 (multiple, 2 regs, full)" recip TP 0.667 for 2 registers, so
// the same 3 Q-loads/cycle, but half the load INSTRUCTIONS and half the address
// arithmetic).
//
// If this ties neon_u4, the kernel is issue bound as derived and MRxNR register
// blocking will not help either -- which is the actual thing worth knowing,
// because it is a cheap proxy for a much more expensive experiment.
uint64_t bb_neon_ld2(const BitmapView& a, const BitmapView& b) {
    const uint8_t* pa = (const uint8_t*)a.w;
    const uint8_t* pb = (const uint8_t*)b.w;
    const uint32_t nb = a.nw * 8;

    uint16x8_t acc0 = vdupq_n_u16(0), acc1 = vdupq_n_u16(0);
    uint16x8_t acc2 = vdupq_n_u16(0), acc3 = vdupq_n_u16(0);
    uint32_t i = 0;
    for (; i + 64 <= nb; i += 64) {
        const uint8x16x2_t a01 = vld1q_u8_x2(pa + i);
        const uint8x16x2_t b01 = vld1q_u8_x2(pb + i);
        const uint8x16x2_t a23 = vld1q_u8_x2(pa + i + 32);
        const uint8x16x2_t b23 = vld1q_u8_x2(pb + i + 32);
        acc0 = vpadalq_u8(acc0, vcntq_u8(vandq_u8(a01.val[0], b01.val[0])));
        acc1 = vpadalq_u8(acc1, vcntq_u8(vandq_u8(a01.val[1], b01.val[1])));
        acc2 = vpadalq_u8(acc2, vcntq_u8(vandq_u8(a23.val[0], b23.val[0])));
        acc3 = vpadalq_u8(acc3, vcntq_u8(vandq_u8(a23.val[1], b23.val[1])));
    }
    uint32x4_t s32 = vpadalq_u16(vdupq_n_u32(0), acc0);
    s32 = vpadalq_u16(s32, acc1);
    s32 = vpadalq_u16(s32, acc2);
    s32 = vpadalq_u16(s32, acc3);
    uint16x8_t tail = vdupq_n_u16(0);
    for (; i + 16 <= nb; i += 16)
        tail = vpadalq_u8(tail, vcntq_u8(vandq_u8(vld1q_u8(pa + i), vld1q_u8(pb + i))));
    s32 = vpadalq_u16(s32, tail);
    uint64_t c = vaddvq_u64(vpadalq_u32(vdupq_n_u64(0), s32));
    for (uint32_t k = i / 8; k < a.nw; ++k) c += STORM_POPCOUNT(a.w[k] & b.w[k]);
    return c;
}

// --- V7c: prefetched, for the DRAM-resident regime ---------------------------
// On the long-run corpus (6 MB working set, past this host's 4 MB L2) EVERY
// hand-written NEON variant here loses to bb_scalar -- 0.58x to 0.80x. That is
// backwards: clang auto-vectorizes the scalar loop into the same instructions,
// so the gap cannot be the instruction mix. The remaining difference is that
// the auto-vectorized loop is software-pipelined and these are not, which only
// matters once the loads miss cache.
//
// This variant is neon_u4 with an explicit prefetch a cache line ahead on both
// operands. If it closes the gap, the cause was memory-level parallelism and
// the cache-resident tuning simply does not transfer to the streaming regime --
// which would mean the B x B cell needs TWO kernels selected on working-set
// size, not one. That is a more interesting result than another 2% on a
// register allocation.
uint64_t bb_neon_pf(const BitmapView& a, const BitmapView& b) {
    const uint8_t* pa = (const uint8_t*)a.w;
    const uint8_t* pb = (const uint8_t*)b.w;
    const uint32_t nb = a.nw * 8;
    constexpr uint32_t PF = 512;   // bytes ahead: 8 cache lines

    uint16x8_t c0 = vdupq_n_u16(0), c1 = vdupq_n_u16(0);
    uint16x8_t c2 = vdupq_n_u16(0), c3 = vdupq_n_u16(0);
    uint32_t i = 0;
    for (; i + 64 <= nb; i += 64) {
        if (i + PF < nb) {
            __builtin_prefetch(pa + i + PF, 0, 0);
            __builtin_prefetch(pb + i + PF, 0, 0);
        }
        c0 = vpadalq_u8(c0, vcntq_u8(vandq_u8(vld1q_u8(pa + i),      vld1q_u8(pb + i))));
        c1 = vpadalq_u8(c1, vcntq_u8(vandq_u8(vld1q_u8(pa + i + 16), vld1q_u8(pb + i + 16))));
        c2 = vpadalq_u8(c2, vcntq_u8(vandq_u8(vld1q_u8(pa + i + 32), vld1q_u8(pb + i + 32))));
        c3 = vpadalq_u8(c3, vcntq_u8(vandq_u8(vld1q_u8(pa + i + 48), vld1q_u8(pb + i + 48))));
    }
    uint32x4_t s32 = vpadalq_u16(vdupq_n_u32(0), c0);
    s32 = vpadalq_u16(s32, c1);
    s32 = vpadalq_u16(s32, c2);
    s32 = vpadalq_u16(s32, c3);
    uint16x8_t tail = vdupq_n_u16(0);
    for (; i + 16 <= nb; i += 16)
        tail = vpadalq_u8(tail, vcntq_u8(vandq_u8(vld1q_u8(pa + i), vld1q_u8(pb + i))));
    s32 = vpadalq_u16(s32, tail);
    uint64_t c = vaddvq_u64(vpadalq_u32(vdupq_n_u64(0), s32));
    for (uint32_t k = i / 8; k < a.nw; ++k) c += STORM_POPCOUNT(a.w[k] & b.w[k]);
    return c;
}

// --- V8: rank-gated block skipping -----------------------------------------
// The one B x B variant that engages the project's actual thesis. The rank
// index already stores a prefix popcount every 8 words, so rank[k+1] == rank[k]
// proves those 8 words (512 bits, 64 bytes) are entirely zero on that side, and
// the whole block can be skipped with one integer compare instead of 4 AND +
// 4 CNT + 4 UADALP.
//
// This is NOT the same as the asymmetric cells: it still costs Theta(m/8)
// compares, so it is Theta(m) with a smaller constant, not sub-linear. It
// cannot reach what B x S or B x R reach. It is here to quantify how much of
// the sparse-side win is available without leaving the bitmap representation at
// all -- which is exactly the "why not just skip zeros?" question a reviewer
// will ask about PROBLEM_STATEMENT.md 2.
uint64_t bb_neon_rankskip(const BitmapView& a, const BitmapView& b) {
    if (a.rank == nullptr || b.rank == nullptr) return bb_neon_unroll<4>(a, b);

    const uint32_t S = BitmapView::RANK_STRIDE;
    const uint32_t nblk = a.nw / S;
    uint16x8_t acc = vdupq_n_u16(0);
    uint64_t   c   = 0;

    for (uint32_t blk = 0; blk < nblk; ++blk) {
        if (rank_block_empty(a, blk)) continue;   // 8 all-zero words on A
        if (rank_block_empty(b, blk)) continue;   // 8 all-zero words on B
        const uint8_t* pa = (const uint8_t*)(a.w + blk * S);
        const uint8_t* pb = (const uint8_t*)(b.w + blk * S);
        for (int q = 0; q < 4; ++q) {
            const uint8x16_t x = vandq_u8(vld1q_u8(pa + 16 * q), vld1q_u8(pb + 16 * q));
            acc = vpadalq_u8(acc, vcntq_u8(x));
        }
        // 4 UADALP of at most 8 per lane = 32 per block; u16 saturates after
        // 2047 non-skipped blocks. Flush conservatively.
        if ((blk & 1023) == 1023) {
            c += vaddvq_u64(vpadalq_u32(vdupq_n_u64(0), vpadalq_u16(vdupq_n_u32(0), acc)));
            acc = vdupq_n_u16(0);
        }
    }
    c += vaddvq_u64(vpadalq_u32(vdupq_n_u64(0), vpadalq_u16(vdupq_n_u32(0), acc)));
    for (uint32_t k = nblk * S; k < a.nw; ++k) c += STORM_POPCOUNT(a.w[k] & b.w[k]);
    return c;
}

#endif // STORM_CELL_NEON

// --- V9: rank-skip driven by the SPARSER side only --------------------------
// bb_neon_rankskip tests both sides' rank blocks. That is two loads per block to
// avoid four AND+CNT+UADALP triples -- worth it when a block is empty, pure
// overhead when it is not, which is why it measures 0.40x on dense data.
//
// Under a skewed spectrum one side is usually far sparser than the other, so
// testing only THAT side captures nearly all the skippable blocks for half the
// index traffic. Which side is sparser is O(1) from the rank totals.
uint64_t bb_neon_rankskip1(const BitmapView& a, const BitmapView& b) {
    if (a.rank == nullptr || b.rank == nullptr) return bb_neon_unroll<8>(a, b);
    const uint32_t S = BitmapView::RANK_STRIDE;
    const uint32_t nblk = a.nw / S;
    // Total set bits are the sentinel entries; pick the sparser side to gate on.
    const uint64_t ta = a.rank[2 * ((a.nw + 7) / 8)];
    const uint64_t tb = b.rank[2 * ((b.nw + 7) / 8)];
    const BitmapView& g = (ta <= tb) ? a : b;

    uint16x8_t acc = vdupq_n_u16(0);
    uint64_t   c   = 0;
    for (uint32_t blk = 0; blk < nblk; ++blk) {
        if (rank_block_empty(g, blk)) continue;
        const uint8_t* pa = (const uint8_t*)(a.w + blk * S);
        const uint8_t* pb = (const uint8_t*)(b.w + blk * S);
        for (int q = 0; q < 4; ++q)
            acc = vpadalq_u8(acc, vcntq_u8(vandq_u8(vld1q_u8(pa + 16 * q),
                                                    vld1q_u8(pb + 16 * q))));
        if ((blk & 1023) == 1023) {
            c += vaddvq_u64(vpadalq_u32(vdupq_n_u64(0), vpadalq_u16(vdupq_n_u32(0), acc)));
            acc = vdupq_n_u16(0);
        }
    }
    c += vaddvq_u64(vpadalq_u32(vdupq_n_u64(0), vpadalq_u16(vdupq_n_u32(0), acc)));
    for (uint32_t k = nblk * S; k < a.nw; ++k) c += STORM_POPCOUNT(a.w[k] & b.w[k]);
    return c;
}

/* --- V10: zone-map planning ------------------------------------------------
 *
 * The Parquet/ORC idea applied to pairing. `occ` is one bit per 512-bit bin,
 * set when that bin holds anything (storm_repr.h). Then:
 *
 *     occ_A & occ_B   -> the bins where BOTH sides have content
 *     popcount(that)  -> how many bins need visiting; zero means disjoint rows
 *
 * The AND runs over m/512 bits, so deciding what work to do costs 1/512 of
 * doing it. Every bin not in the intersection is skipped entirely -- not
 * processed faster, not processed at all.
 *
 * Two things distinguish this from neon_rankskip, which also skips empty
 * regions:
 *
 *  1. It gates on BOTH sides at once, from one AND, rather than testing each
 *     side's index separately. A bin survives only if both rows occupy it,
 *     which is a strictly stronger filter than either test alone.
 *  2. The summary is 0.195% overhead against rank9's 25%, and 512x smaller than
 *     the data, so the whole zone map of a row stays in L1 while its bitmap
 *     does not. The scan that decides the work is cache-resident even when the
 *     work itself is not.
 *
 * Build cost is one forward pass over the row, O(m), paid once and amortized
 * over every pairing that row participates in -- N-1 of them in an all-pairs
 * problem. At N = 10^5 that is a 10^-5 amortized surcharge. In a real system it
 * would not be paid at query time at all: it is exactly the kind of summary a
 * columnar format stores with the data.
 *
 * This is the mechanism that makes work REDUCIBLE, which finding F5 says is the
 * only thing that has ever won outside the dense cell. It belongs in the
 * selection layer (M2) rather than in a kernel; it lives here so its value can
 * be measured before that layer exists.
 */
uint64_t bb_occ(const BitmapView& a, const BitmapView& b) {
    if (a.occ == nullptr || b.occ == nullptr) return bb_neon_unroll<8>(a, b);
    const uint32_t BW = BitmapView::OCC_BIN_WORDS;
    const uint32_t full_bins = a.nw / BW;              // bins backed by whole words
    const uint32_t n_occ = std::min(a.n_occ, b.n_occ);

    uint16x8_t acc = vdupq_n_u16(0);
    uint64_t   c   = 0;
    uint32_t   since_flush = 0;

    for (uint32_t ow = 0; ow < n_occ; ++ow) {
        uint64_t m = a.occ[ow] & b.occ[ow];            // bins live on BOTH sides
        while (m) {
            const uint32_t bin = ow * 64 + (uint32_t)__builtin_ctzll(m);
            m &= m - 1;
            if (bin >= full_bins) continue;            // partial tail bin: below
            const uint8_t* pa = (const uint8_t*)(a.w + (size_t)bin * BW);
            const uint8_t* pb = (const uint8_t*)(b.w + (size_t)bin * BW);
            for (int q = 0; q < 4; ++q)
                acc = vpadalq_u8(acc, vcntq_u8(vandq_u8(vld1q_u8(pa + 16 * q),
                                                        vld1q_u8(pb + 16 * q))));
            // 4 UADALP of at most 8 per lane = 32 per bin; u16 saturates at 2047.
            if (++since_flush == 1024) {
                c += vaddvq_u64(vpadalq_u32(vdupq_n_u64(0), vpadalq_u16(vdupq_n_u32(0), acc)));
                acc = vdupq_n_u16(0);
                since_flush = 0;
            }
        }
    }
    c += vaddvq_u64(vpadalq_u32(vdupq_n_u64(0), vpadalq_u16(vdupq_n_u32(0), acc)));
    for (uint32_t k = full_bins * BW; k < a.nw; ++k) c += STORM_POPCOUNT(a.w[k] & b.w[k]);
    return c;
}

/* The planning step on its own, with the pair-level early out made explicit.
 * If the zone maps do not intersect the rows cannot, and the pair is settled in
 * m/512 bits of work with the bitmaps never touched. Separated from bb_occ so
 * the early out's contribution is visible rather than folded into the scan. */
uint64_t bb_occ_plan(const BitmapView& a, const BitmapView& b) {
    if (a.occ == nullptr || b.occ == nullptr) return bb_neon_unroll<8>(a, b);
    if (occ_overlap(a, b) == 0) return 0;
    return bb_occ(a, b);
}

/* --- V11: plan from the zone map, then execute -----------------------------
 *
 * bb_occ wins 1.7x to 47x on skewed, clustered and run-structured data and
 * LOSES ~3x on uniform-density data. That is not a defect, it is what a filter
 * does: when every bin is occupied the summary filters nothing and the
 * bit-scan is pure overhead on top of the same work.
 *
 * So the zone map should not be consulted as a kernel. It should be consulted
 * as a PLAN. `popcount(occ_A & occ_B)` is one pass over m/512 bits and yields
 * the exact number of bins that need visiting -- not an estimate. Comparing it
 * to the bin count gives the selectivity, and the choice follows:
 *
 *     selectivity == 0            -> the rows are disjoint; return 0
 *     selectivity <  threshold    -> visit only the live bins
 *     otherwise                   -> straight-line SIMD over everything
 *
 * The threshold is where per-bin dispatch overhead equals the work it avoids.
 * Measured: bb_occ costs ~0.079 cyc/word when it skips almost everything and
 * ~1.29 when it skips nothing, against ~0.42 for the straight-line kernel, so
 * the crossover sits near a third of bins live. Named and calibrated in one
 * place; it belongs in the cost model (M4) like every other threshold here.
 *
 * This is the "move up towards planning rather than work" step in miniature:
 * the decision costs 0.2% of the execution it is deciding about, and it is
 * exact rather than heuristic.
 */
constexpr double kOccPlanSelectivity = 0.33;

uint64_t bb_occ_sel(const BitmapView& a, const BitmapView& b) {
    if (a.occ == nullptr || b.occ == nullptr) return bb_neon_unroll<8>(a, b);

    const uint32_t n_occ = std::min(a.n_occ, b.n_occ);
    const uint32_t bins  = (a.nw + BitmapView::OCC_BIN_WORDS - 1) / BitmapView::OCC_BIN_WORDS;
    uint64_t live = 0;
    for (uint32_t i = 0; i < n_occ; ++i) live += STORM_POPCOUNT(a.occ[i] & b.occ[i]);

    if (live == 0) return 0;                                   // provably disjoint
    if (bins && (double)live < kOccPlanSelectivity * (double)bins)
        return bb_occ(a, b);                                   // sparse overlap
    return bb_neon_unroll<8>(a, b);                            // dense overlap
}

const Variant<fn_bb> kBB[] = {
    {"scalar",       bb_scalar,     "reference: 1 word at a time"},
    {"scalar_u4",    bb_scalar_u4,  "libalgebra unrolled -- what the repo shipped on arm64"},
#if STORM_CELL_NEON
    {"neon",         bb_neon,               "AND+CNT+UADALP, 1 accumulator"},
    {"neon_u2",      bb_neon_unroll<2>,     "2 accumulators"},
    {"neon_u4",      bb_neon_unroll<4>,     "4 accumulators -- predicted first to cover UADALP lat 3"},
    {"neon_u8",      bb_neon_unroll<8>,     "8 accumulators"},
    {"neon_u16",     bb_neon_unroll<16>,    "16 accumulators -- has the series saturated?"},
    {"neon_u8acc4",  bb_neon_u8acc<4>,      "u8 accumulate, widen every 31 -- shorter recurrence"},
    {"neon_u8acc8",  bb_neon_u8acc<8>,      "u8 accumulate, 8 accumulators"},
    {"neon_ld2",     bb_neon_ld2,           "vld1q_u8_x2 paired loads -- issue-bound probe"},
    {"neon_pf",      bb_neon_pf,            "4 accumulators + prefetch, for the DRAM-resident regime"},
    {"neon_hs",      bb_neon_harley_seal,   "Harley-Seal CSA -- labelled negative control"},
    {"neon_rankskip",bb_neon_rankskip,      "skip all-zero 512b blocks via rank index", true},
    {"occ",          bb_occ,                "zone map: visit only bins live on BOTH sides", true},
    {"occ_sel",      bb_occ_sel,            "PLAN from the zone map, then pick the kernel", true},
    {"occ_plan",     bb_occ_plan,           "zone map + explicit disjoint-pair early out", true},
    {"neon_rskip1",  bb_neon_rankskip1,     "gate on the sparser side only -- half the index traffic", true},
#endif
};

} // namespace

VariantList<fn_bb> cell_bb() { return {kBB, sizeof(kBB) / sizeof(kBB[0])}; }

} // namespace storm
