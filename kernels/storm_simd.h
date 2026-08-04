/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist
 * Licensed under the Apache License, Version 2.0.
 *
 * Shared SIMD primitives for the cell kernels.
 *
 * These exist because the first cut of the WAH and run cells each carried a
 * private single-accumulator popcount helper, and every one of them LOST to the
 * "scalar" reference -- B x W measured 63.78 ns against scalar's 47.13. The
 * cause was not the vectorization but the recurrence: one accumulator with a
 * latency-3 UADALP retires one 16-byte chunk every 3 cycles, while clang
 * auto-vectorizes the plain `c += POPCOUNT(w[k])` loop with several independent
 * accumulators and beats it. A hand-written NEON kernel that loses to the
 * compiler's version of the same loop is a bug, not a finding.
 *
 * Preflight (AGENTS.md evidence ladder), all tier 2 -- sourced:
 *   vcntq_u8    -> CNT    Vd.16B,Vn.16B   v7/A32/A64   corpus bit-manipulation/
 *   vandq_u8    -> AND    Vd.16B,Vn.16B   v7/A32/A64   corpus logical/
 *   vpadalq_u8  -> UADALP Vd.8H,Vn.16B    v7/A32/A64   corpus vector-arithmetic/
 *   vpadalq_u16 -> UADALP Vd.4S,Vn.8H     v7/A32/A64
 *   vpadalq_u32 -> UADALP Vd.2D,Vn.4S     v7/A32/A64
 *   vaddvq_u64  -> ADDP   Dd,Vn.2D        A64
 * Baseline Advanced SIMD throughout; no extension gate, no -m flag required.
 *
 * Firestorm resources (applecpu, tier 2, and Firestorm is NOT this host):
 *   CNT/AND/UADALP  recip TP 0.25 on 4 pipes (u11-14), UADALP latency 3.
 * Hence 4 accumulators is the minimum to cover the UADALP recurrence and 4 is
 * what these use. Confirmed by measurement on M4 in the B x B cell, where
 * neon_u4 (1.23x) and neon_u8 (1.28x) both beat neon (0.40x) with the identical
 * instruction mix and only the accumulator count changed.
 */
#ifndef STORM_SIMD_H_
#define STORM_SIMD_H_

#include <cstdint>
#include "libalgebra/libalgebra.h"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#  include <arm_neon.h>
#  define STORM_SIMD_NEON 1
#else
#  define STORM_SIMD_NEON 0
#endif

namespace storm {

#if STORM_SIMD_NEON
static inline uint64_t simd_reduce_u16(uint16x8_t a, uint16x8_t b,
                                       uint16x8_t c, uint16x8_t d) {
    uint32x4_t s = vpadalq_u16(vdupq_n_u32(0), a);
    s = vpadalq_u16(s, b);
    s = vpadalq_u16(s, c);
    s = vpadalq_u16(s, d);
    return vaddvq_u64(vpadalq_u32(vdupq_n_u64(0), s));
}
#endif

// popcount over n 64-bit words.
static inline uint64_t simd_popcnt(const uint64_t* w, uint32_t n) {
#if STORM_SIMD_NEON
    const uint8_t* p = (const uint8_t*)w;
    const uint32_t nb = n * 8;
    uint16x8_t a0 = vdupq_n_u16(0), a1 = vdupq_n_u16(0);
    uint16x8_t a2 = vdupq_n_u16(0), a3 = vdupq_n_u16(0);
    uint32_t i = 0;
    for (; i + 64 <= nb; i += 64) {
        a0 = vpadalq_u8(a0, vcntq_u8(vld1q_u8(p + i)));
        a1 = vpadalq_u8(a1, vcntq_u8(vld1q_u8(p + i + 16)));
        a2 = vpadalq_u8(a2, vcntq_u8(vld1q_u8(p + i + 32)));
        a3 = vpadalq_u8(a3, vcntq_u8(vld1q_u8(p + i + 48)));
    }
    for (; i + 16 <= nb; i += 16) a0 = vpadalq_u8(a0, vcntq_u8(vld1q_u8(p + i)));
    uint64_t c = simd_reduce_u16(a0, a1, a2, a3);
    for (uint32_t k = i / 8; k < n; ++k) c += STORM_POPCOUNT(w[k]);
    return c;
#else
    uint64_t c0 = 0, c1 = 0, c2 = 0, c3 = 0;
    uint32_t k = 0;
    for (; k + 4 <= n; k += 4) {
        c0 += STORM_POPCOUNT(w[k]);     c1 += STORM_POPCOUNT(w[k + 1]);
        c2 += STORM_POPCOUNT(w[k + 2]); c3 += STORM_POPCOUNT(w[k + 3]);
    }
    uint64_t c = c0 + c1 + c2 + c3;
    for (; k < n; ++k) c += STORM_POPCOUNT(w[k]);
    return c;
#endif
}

// popcount of the AND of two n-word arrays.
static inline uint64_t simd_and_popcnt(const uint64_t* a, const uint64_t* b, uint32_t n) {
#if STORM_SIMD_NEON
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    const uint32_t nb = n * 8;
    uint16x8_t a0 = vdupq_n_u16(0), a1 = vdupq_n_u16(0);
    uint16x8_t a2 = vdupq_n_u16(0), a3 = vdupq_n_u16(0);
    uint32_t i = 0;
    for (; i + 64 <= nb; i += 64) {
        a0 = vpadalq_u8(a0, vcntq_u8(vandq_u8(vld1q_u8(pa + i),      vld1q_u8(pb + i))));
        a1 = vpadalq_u8(a1, vcntq_u8(vandq_u8(vld1q_u8(pa + i + 16), vld1q_u8(pb + i + 16))));
        a2 = vpadalq_u8(a2, vcntq_u8(vandq_u8(vld1q_u8(pa + i + 32), vld1q_u8(pb + i + 32))));
        a3 = vpadalq_u8(a3, vcntq_u8(vandq_u8(vld1q_u8(pa + i + 48), vld1q_u8(pb + i + 48))));
    }
    for (; i + 16 <= nb; i += 16)
        a0 = vpadalq_u8(a0, vcntq_u8(vandq_u8(vld1q_u8(pa + i), vld1q_u8(pb + i))));
    uint64_t c = simd_reduce_u16(a0, a1, a2, a3);
    for (uint32_t k = i / 8; k < n; ++k) c += STORM_POPCOUNT(a[k] & b[k]);
    return c;
#else
    uint64_t c0 = 0, c1 = 0, c2 = 0, c3 = 0;
    uint32_t k = 0;
    for (; k + 4 <= n; k += 4) {
        c0 += STORM_POPCOUNT(a[k]     & b[k]);
        c1 += STORM_POPCOUNT(a[k + 1] & b[k + 1]);
        c2 += STORM_POPCOUNT(a[k + 2] & b[k + 2]);
        c3 += STORM_POPCOUNT(a[k + 3] & b[k + 3]);
    }
    uint64_t c = c0 + c1 + c2 + c3;
    for (; k < n; ++k) c += STORM_POPCOUNT(a[k] & b[k]);
    return c;
#endif
}

} // namespace storm

#endif // STORM_SIMD_H_
