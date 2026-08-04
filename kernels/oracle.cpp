/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist
 * Licensed under the Apache License, Version 2.0.
 *
 * The correctness oracle. RESEARCH_PLAN.md 7.4: "naive scalar, obviously
 * correct". Its only job is to be independent of everything it checks, so it
 * deliberately shares no helper with any kernel — not even the popcount
 * wrapper — and rebuilds both sides from the position list rather than reusing
 * any converter. If a converter in storm_repr.cpp is wrong, this still catches
 * it; a "just decode the representations and AND" oracle would not.
 */
#include "kernels/storm_cells.h"

#include <vector>

namespace storm {

namespace {
// Independent popcount. Not STORM_POPCOUNT, not __builtin_popcountll: if the
// build picks a broken intrinsic path, the oracle must not pick it too.
uint32_t oracle_popcount(uint64_t x) {
    uint32_t n = 0;
    while (x) { n += (uint32_t)(x & 1u); x >>= 1; }
    return n;
}
} // namespace

uint64_t oracle_intersect(const Row& a, const Row& b) {
    const uint32_t nw = a.meta.n_words < b.meta.n_words ? a.meta.n_words : b.meta.n_words;

    std::vector<uint64_t> ba(nw, 0), bb(nw, 0);
    for (uint32_t p : a.list) if ((p >> 6) < nw) ba[p >> 6] |= uint64_t(1) << (p & 63);
    for (uint32_t p : b.list) if ((p >> 6) < nw) bb[p >> 6] |= uint64_t(1) << (p & 63);

    uint64_t c = 0;
    for (uint32_t k = 0; k < nw; ++k) c += oracle_popcount(ba[k] & bb[k]);
    return c;
}

} // namespace storm
