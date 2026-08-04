/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist
 * Licensed under the Apache License, Version 2.0.
 */
#include "kernels/storm_gen.h"

#include <algorithm>
#include <cmath>

namespace storm {

const char* name_of(Structure s) {
    switch (s) {
        case Structure::Uniform:   return "uniform";
        case Structure::Clustered: return "clustered";
        case Structure::Runs:      return "runs";
    }
    return "?";
}

const char* name_of(Spectrum s) {
    switch (s) {
        case Spectrum::Uniform: return "uniform";
        case Spectrum::Inverse: return "inverse";   // 1/i
        case Spectrum::Bimodal: return "bimodal";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Spectrum (ii): the across-row cardinality distribution.
// ---------------------------------------------------------------------------

/* For the 1/i spectrum we draw cardinality log-uniformly on [1, C], which gives
 * P(c) proportional to 1/c exactly — the neutral-coalescent site frequency
 * spectrum of PROBLEM_STATEMENT.md 2.2.
 *
 * The upper limit C is not free: the whole point of comparing the uniform and
 * 1/i spectra is that they do the SAME total work in bitmap space and differ
 * only in how that work is distributed. So C is solved such that the mean of
 * the 1/i draw equals the target mean cardinality T:
 *
 *     E[c] = (C - 1) / ln C = T
 *
 * Without this the 1/i corpus is simply sparser than the uniform one and the
 * comparison measures density, not skew — which would flatter the result for
 * the wrong reason. */
static double solve_inverse_limit(double T) {
    if (T <= 1.0) return 1.0;
    double lo = 1.0 + 1e-9, hi = 4.0;
    while ((hi - 1.0) / std::log(hi) < T) { hi *= 2.0; if (hi > 1e18) break; }
    for (int it = 0; it < 200; ++it) {
        const double mid = 0.5 * (lo + hi);
        if ((mid - 1.0) / std::log(mid) < T) lo = mid; else hi = mid;
    }
    return 0.5 * (lo + hi);
}

static uint32_t draw_cardinality(Rng& rng, const CorpusSpec& spec,
                                 double T, double inv_limit)
{
    uint32_t c = 0;
    switch (spec.spectrum) {
        case Spectrum::Uniform:
            c = (uint32_t)(T + 0.5);
            break;
        case Spectrum::Inverse: {
            const double u = rng.unit();
            c = (uint32_t)std::floor(std::exp(u * std::log(inv_limit)));
            break;
        }
        case Spectrum::Bimodal: {
            // 10% dense / 90% singleton-ish, means matched to T.
            // T = 0.1*hi + 0.9*lo, with lo pinned at 1.
            const double hi = (T - 0.9) / 0.1;
            c = (rng.unit() < 0.1) ? (uint32_t)(hi > 1 ? hi : 1) : 1u;
            break;
        }
    }
    if (c < 1) c = 1;
    if (c > spec.universe) c = spec.universe;
    return c;
}

// ---------------------------------------------------------------------------
// Structure (i): the within-row layout.
// ---------------------------------------------------------------------------

// Exactly `c` distinct positions, uniform over [0, universe).
static void gen_uniform(Rng& rng, uint32_t c, uint32_t universe,
                        std::vector<uint32_t>& out, std::vector<uint64_t>& mark)
{
    out.clear();
    const uint32_t nw = (universe + 63u) / 64u;
    // Sampling the complement when c is more than half the universe keeps the
    // rejection loop's expected draw count at or below 2c either way.
    const bool complement = (c > universe / 2);
    const uint32_t want   = complement ? universe - c : c;

    std::fill(mark.begin(), mark.begin() + nw, 0ull);
    uint32_t got = 0;
    while (got < want) {
        const uint32_t p = rng.below(universe);
        if ((mark[p >> 6] >> (p & 63)) & 1u) continue;
        mark[p >> 6] |= uint64_t(1) << (p & 63);
        ++got;
    }
    for (uint32_t p = 0; p < universe; ++p) {
        const bool set = ((mark[p >> 6] >> (p & 63)) & 1u) != 0;
        if (set != complement) out.push_back(p);
    }
}

/* Markov gap model. Alternates geometric gaps and geometric runs, which is the
 * same generative process as a two-state Markov chain over bit positions but
 * costs O(runs) rather than O(universe) — the difference matters at the 10^7-bit
 * universes of PROBLEM_STATEMENT.md 2.
 *
 * mean_run L is set by the caller (from the clustering coefficient, or directly).
 * The mean gap is then fixed by the target density d:  G = L (1 - d) / d. */
static void gen_clustered(Rng& rng, uint32_t c, uint32_t universe,
                          double mean_run, std::vector<uint32_t>& out)
{
    out.clear();
    if (c == 0) return;

    const double d = (double)c / (double)universe;
    double L = mean_run;
    if (L < 1.0) L = 1.0;
    if (L > (double)c) L = (double)c;           // cannot have longer runs than bits
    const double G = (d >= 1.0) ? 1.0 : L * (1.0 - d) / d;

    // Geometric with mean m, support >= 1.
    auto geom = [&rng](double m) -> uint32_t {
        if (m <= 1.0) return 1;
        const double p = 1.0 / m;
        double u = rng.unit();
        if (u <= 0.0) u = 1e-18;
        const double g = std::log(u) / std::log(1.0 - p);
        const double r = std::floor(g) + 1.0;
        return (uint32_t)(r < 1.0 ? 1.0 : (r > 4e9 ? 4e9 : r));
    };

    uint64_t pos = 0;
    uint32_t got = 0;
    while (got < c && pos < universe) {
        pos += geom(G);
        if (pos >= universe) break;
        uint32_t len = geom(L);
        if (got + len > c)              len = c - got;
        if (pos + len > universe)       len = (uint32_t)(universe - pos);
        for (uint32_t k = 0; k < len; ++k) out.push_back((uint32_t)(pos + k));
        got += len;
        pos += len;
    }
    // If the walk ran off the end of the universe before placing c bits, top up
    // from the front so the realized cardinality still matches the spectrum draw.
    if (got < c) {
        uint32_t p = 0;
        size_t   i = 0;
        while (got < c && p < universe) {
            while (i < out.size() && out[i] < p) ++i;
            if (i < out.size() && out[i] == p) { ++p; continue; }
            out.push_back(p);
            ++got; ++p;
        }
        std::sort(out.begin(), out.end());
    }
}

// ---------------------------------------------------------------------------

void generate(Corpus& out, const CorpusSpec& spec) {
    out.spec = spec;
    out.rows.clear();
    out.rows.resize(spec.n_rows);

    const double T         = spec.density * (double)spec.universe;
    const double inv_limit = solve_inverse_limit(T);
    const uint32_t nw      = (spec.universe + 63u) / 64u;

    Rng rng(spec.seed);
    std::vector<uint32_t> pos;
    std::vector<uint64_t> mark(nw);

    double   sum_card = 0, sum_runs = 0;
    uint32_t mn = UINT32_MAX, mx = 0;
    double   bB = 0, bS = 0, bR = 0, bW = 0, bK = 0, bO = 0;

    for (uint32_t i = 0; i < spec.n_rows; ++i) {
        const uint32_t c = draw_cardinality(rng, spec, T, inv_limit);

        switch (spec.structure) {
            case Structure::Uniform:
                gen_uniform(rng, c, spec.universe, pos, mark);
                break;
            case Structure::Clustered: {
                // clustering is P(a run continues); mean run length = 1/(1-p).
                double p = spec.clustering;
                if (p < 0.0) p = 0.0;
                if (p > 0.999999) p = 0.999999;
                gen_clustered(rng, c, spec.universe, 1.0 / (1.0 - p), pos);
                break;
            }
            case Structure::Runs:
                gen_clustered(rng, c, spec.universe, (double)spec.mean_run, pos);
                break;
        }

        out.rows[i].occ_bin = spec.occ_bin;
        build_row(out.rows[i], pos.data(), pos.size(), spec.universe);

        const RowMeta& m = out.rows[i].meta;
        sum_card += m.cardinality;
        sum_runs += m.n_runs;
        mn = std::min(mn, m.cardinality);
        mx = std::max(mx, m.cardinality);
        bB += (double)m.n_words * 8.0;
        bS += (double)m.cardinality * 4.0;
        bR += (double)m.n_runs * 8.0;
        bW += (double)out.rows[i].ewah.size() * 8.0;
        bK += (double)out.rows[i].rank.size() * 8.0;
        bO += (double)out.rows[i].occ.size()  * 8.0;
    }

    out.mean_card = sum_card / spec.n_rows;
    out.mean_runs = sum_runs / spec.n_rows;
    out.min_card  = (mn == UINT32_MAX) ? 0 : mn;
    out.max_card  = mx;
    out.bytes_B = bB; out.bytes_S = bS; out.bytes_R = bR; out.bytes_W = bW;
    out.bytes_rank = bK; out.bytes_occ = bO;
}

} // namespace storm
