/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist
 * Licensed under the Apache License, Version 2.0.
 *
 * Corpus generators — RESEARCH_PLAN.md 7.2.
 *
 * Two INDEPENDENT axes, and both must be swept. Collapsing them is the single
 * easiest way to benchmark this project's contribution away:
 *
 *   (i)  within-row structure  — how set bits are laid out inside one row.
 *        Uniform-random is the worst case for clustering and is one point in
 *        the study, not the study.
 *
 *   (ii) across-row spectrum   — how cardinality is distributed over the corpus.
 *        A uniform spectrum gives every row the same density, which never
 *        generates the skew that motivates adaptive pairing at all. The 1/i
 *        spectrum is mandatory (PROBLEM_STATEMENT.md 2.2); the gap between the
 *        two IS the result.
 */
#ifndef STORM_GEN_H_
#define STORM_GEN_H_

#include <cstdint>
#include <vector>
#include <string>

#include "kernels/storm_repr.h"

namespace storm {

// (i) within-row structure
enum class Structure {
    Uniform,      // uniform-random positions
    Clustered,    // Markov gap model, tunable autocorrelation
    Runs,         // explicit blocks of set bits — where R and W should win
};

// (ii) across-row cardinality spectrum
enum class Spectrum {
    Uniform,      // every row the same cardinality. The misleading classic.
    Inverse,      // 1/i — neutral coalescent, singleton-dominated. MANDATORY.
    Bimodal,      // dense minority + sparse majority; stresses the mixed cell
};

struct CorpusSpec {
    uint32_t  n_rows    = 1024;
    uint32_t  universe  = 1u << 16;
    double    density   = 0.01;    // mean set fraction, before the spectrum tilts it
    Structure structure = Structure::Uniform;
    Spectrum  spectrum  = Spectrum::Uniform;
    double    clustering = 0.9;    // Structure::Clustered: P(next bit continues a run)
    uint32_t  mean_run  = 32;      // Structure::Runs: mean run length in bits
    uint64_t  seed      = 0x5eed1234u;
};

struct Corpus {
    std::vector<Row> rows;
    CorpusSpec       spec;

    // Aggregate shape of the generated corpus — reported alongside every result
    // so a reader can see what was actually measured rather than what was asked
    // for. The spectrum can move these a long way from `spec`.
    double   mean_card   = 0;
    double   mean_runs   = 0;
    uint32_t min_card    = 0;
    uint32_t max_card    = 0;
    double   bytes_B     = 0;   // total bitmap bytes
    double   bytes_S     = 0;
    double   bytes_R     = 0;
    double   bytes_W     = 0;
    double   bytes_rank  = 0;   // rank9 index
    double   bytes_occ   = 0;   // zone map
};

void generate(Corpus& out, const CorpusSpec& spec);

const char* name_of(Structure s);
const char* name_of(Spectrum  s);

// Deterministic, cheap, and good enough for corpus generation: splitmix64.
// Named rather than inlined at each use site so every generator draws from the
// same stream shape and a corpus is reproducible from (spec.seed) alone.
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    uint64_t next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    // Unbiased in [0, n).
    uint32_t below(uint32_t n) {
        if (n == 0) return 0;
        const uint64_t lim = UINT64_MAX - (UINT64_MAX % n);
        uint64_t r;
        do { r = next(); } while (r >= lim);
        return (uint32_t)(r % n);
    }
    double unit() { return (double)(next() >> 11) * (1.0 / 9007199254740992.0); }
};

} // namespace storm

#endif // STORM_GEN_H_
