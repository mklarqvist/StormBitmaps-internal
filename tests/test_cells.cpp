/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist
 * Licensed under the Apache License, Version 2.0.
 *
 * Differential test for the pairing-matrix kernel layer.
 *
 * AGENTS.md standing rule 6: every kernel is differential-tested against the
 * scalar oracle, at every blocking factor, on every ISA reachable. And: "a test
 * that cannot fail is worthless". The Phase 0 suite for storm.cpp initially had
 * exactly that defect -- 1,272 checks that passed with the bugs reverted --
 * because it never generated input that reached the broken paths. The lesson is
 * baked into the shape of this file: it sweeps the parameters that select
 * between code paths (structure, spectrum, density, universe alignment) rather
 * than sampling one comfortable corpus.
 *
 * Specifically it guarantees coverage of:
 *   - universes that are not a multiple of 64, 512, or the rank stride
 *   - rows that are empty, full, a single bit, and a single maximal run
 *   - the rank index's sentinel block and its packed 9-bit sub-counters
 *   - EWAH streams that begin with a literal, begin with a fill, and end mid-run
 *   - list/run arrays short enough to skip every vector body and land in tails
 */
#include "kernels/storm_cells.h"
#include "kernels/storm_gen.h"
#include "kernels/storm_allpairs.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace storm;

static uint64_t g_checks = 0;
static uint64_t g_fails  = 0;

// Per-variant tally. Printing only the first N failures hides the distribution,
// and the distribution is the diagnostic: "one variant is broken everywhere" and
// "every variant is broken on one corpus" look identical in a truncated log and
// mean completely different things.
static std::map<std::string, uint64_t>    g_fail_by_variant;
static std::map<std::string, std::string> g_first_ctx;

static void fail(const char* cell, const char* variant, const char* ctx,
                 uint64_t got, uint64_t want)
{
    const std::string key = std::string(cell) + " / " + variant;
    if (g_fail_by_variant[key]++ == 0) {
        g_first_ctx[key] = ctx;
        std::printf("  FAIL %-8s %-14s %s: got %" PRIu64 ", oracle %" PRIu64 "\n",
                    cell, variant, ctx, got, want);
    }
    ++g_fails;
}

template <typename F, typename Apply>
static void check_cell(const char* cell, VariantList<F> vl,
                       const std::vector<Row>& rows, const char* ctx, Apply apply)
{
    for (size_t vi = 0; vi < vl.n; ++vi) {
        for (size_t i = 0; i < rows.size(); ++i) {
            for (size_t j = 0; j < rows.size(); ++j) {
                // Orient by cardinality, as the selection layer would, but run
                // BOTH orders so a kernel that quietly assumes one side is the
                // shorter cannot pass.
                const uint64_t want = oracle_intersect(rows[i], rows[j]);
                const uint64_t got  = apply(vl.v[vi].fn, rows[i], rows[j]);
                ++g_checks;
                if (got != want) fail(cell, vl.v[vi].name, ctx, got, want);
            }
        }
    }
}

// --- representation round-trips --------------------------------------------
// If a converter is wrong every cell that consumes it measures the wrong thing,
// so these are checked before any kernel is.
static void check_representations(const std::vector<Row>& rows, const char* ctx) {
    for (const Row& r : rows) {
        const uint32_t nw = r.meta.n_words;

        // S -> B
        std::vector<uint64_t> b(nw, 0);
        for (uint32_t p : r.list) b[p >> 6] |= uint64_t(1) << (p & 63);
        ++g_checks;
        if (std::memcmp(b.data(), r.bitmap.data(), nw * 8) != 0)
            fail("repr", "bitmap", ctx, 1, 0);

        // R -> B: runs must reproduce the bitmap exactly, and must be maximal
        // (no two runs adjacent or overlapping -- otherwise R x R's overlap
        // arithmetic double-counts).
        std::vector<uint64_t> br(nw, 0);
        for (size_t k = 0; k < r.run_start.size(); ++k) {
            ++g_checks;
            if (k && r.run_start[k] <= r.run_end[k - 1])
                fail("repr", "runs_maximal", ctx, r.run_start[k], r.run_end[k - 1]);
            for (uint32_t p = r.run_start[k]; p < r.run_end[k]; ++p)
                br[p >> 6] |= uint64_t(1) << (p & 63);
        }
        ++g_checks;
        if (std::memcmp(br.data(), r.bitmap.data(), nw * 8) != 0)
            fail("repr", "runs", ctx, 1, 0);

        // W -> B
        std::vector<uint64_t> bw(nw, 0);
        ewah_decode(r.W(), bw.data());
        ++g_checks;
        if (std::memcmp(bw.data(), r.bitmap.data(), nw * 8) != 0)
            fail("repr", "ewah", ctx, 1, 0);

        // rank: exhaustive at every word boundary plus a few interior bits, so
        // both the absolute counters and the packed 9-bit sub-counters are hit.
        const BitmapView bv = r.B();
        uint64_t acc = 0;
        for (uint32_t w = 0; w < nw; ++w) {
            ++g_checks;
            if (rank_at(bv, w * 64) != acc)
                fail("repr", "rank_word", ctx, rank_at(bv, w * 64), acc);
            for (uint32_t bit = 1; bit < 64; bit += 17) {
                const uint64_t mask = (uint64_t(1) << bit) - 1;
                uint64_t want = acc;
                uint64_t x = r.bitmap[w] & mask;
                while (x) { want += (x & 1); x >>= 1; }
                ++g_checks;
                if (rank_at(bv, w * 64 + bit) != want)
                    fail("repr", "rank_bit", ctx, rank_at(bv, w * 64 + bit), want);
            }
            uint64_t x = r.bitmap[w];
            while (x) { acc += (x & 1); x >>= 1; }
        }
        ++g_checks;
        if (rank_at(bv, nw * 64) != acc)
            fail("repr", "rank_end", ctx, rank_at(bv, nw * 64), acc);
        ++g_checks;
        if (acc != r.meta.cardinality)
            fail("repr", "cardinality", ctx, acc, r.meta.cardinality);
    }
}

static void run_all(const std::vector<Row>& rows, const char* ctx) {
    check_representations(rows, ctx);

    check_cell("B x S", cell_bs(), rows, ctx,
        [](fn_bs f, const Row& a, const Row& b) { return f(a.B(), b.S()); });
    check_cell("B x R", cell_br(), rows, ctx,
        [](fn_br f, const Row& a, const Row& b) { return f(a.B(), b.R()); });
    check_cell("B x W", cell_bw(), rows, ctx,
        [](fn_bw f, const Row& a, const Row& b) { return f(a.B(), b.W()); });
    check_cell("S x S", cell_ss(), rows, ctx,
        [](fn_ss f, const Row& a, const Row& b) { return f(a.S(), b.S()); });
    check_cell("S x R", cell_sr(), rows, ctx,
        [](fn_sr f, const Row& a, const Row& b) { return f(a.S(), b.R()); });
    check_cell("S x W", cell_sw(), rows, ctx,
        [](fn_sw f, const Row& a, const Row& b) { return f(a.S(), b.W()); });
    check_cell("R x R", cell_rr(), rows, ctx,
        [](fn_rr f, const Row& a, const Row& b) { return f(a.R(), b.R()); });
    check_cell("R x W", cell_rw(), rows, ctx,
        [](fn_rw f, const Row& a, const Row& b) { return f(a.R(), b.W()); });
    check_cell("W x W", cell_ww(), rows, ctx,
        [](fn_ww f, const Row& a, const Row& b) { return f(a.W(), b.W()); });
    check_cell("B x B", cell_bb(), rows, ctx,
        [](fn_bb f, const Row& a, const Row& b) { return f(a.B(), b.B()); });
    // C x B is only defined when the complement was built (density > 1/2);
    // below that the cell is not selectable and must not be invoked.
    check_cell("C x B", cell_cb(), rows, ctx,
        [](fn_cb f, const Row& a, const Row& b) {
            return a.C().valid ? f(a.C(), b.B()) : oracle_intersect(a, b); });

    // The rank-consuming variants must ALSO be correct when handed a bitmap
    // with no index -- they advertise a fallback and the fallback is a code
    // path like any other.
    check_cell("B x R", cell_br(), rows, ctx,
        [](fn_br f, const Row& a, const Row& b) { return f(a.B_norank(), b.R()); });
    check_cell("B x W", cell_bw(), rows, ctx,
        [](fn_bw f, const Row& a, const Row& b) { return f(a.B_norank(), b.W()); });
    check_cell("B x B", cell_bb(), rows, ctx,
        [](fn_bb f, const Row& a, const Row& b) { return f(a.B_norank(), b.B_norank()); });
}

// Hand-built rows covering the shapes a random corpus reaches only by luck.
static void edge_cases(uint32_t universe) {
    std::vector<Row> rows;
    // build_row's contract is sorted, distinct, and strictly below `universe`.
    // The literals below are written for the largest universe in the sweep, so
    // the filter here is what makes them legal at every smaller one -- without
    // it the small-universe cases scribble past the end of the bitmap and the
    // resulting failures look like kernel bugs.
    auto add = [&](std::vector<uint32_t> v) {
        v.erase(std::remove_if(v.begin(), v.end(),
                               [&](uint32_t p) { return p >= universe; }), v.end());
        rows.emplace_back();
        build_row(rows.back(), v.data(), v.size(), universe);
    };

    add({});                                     // empty
    add({0});                                    // first bit only
    add({universe - 1});                         // last bit only
    add({63, 64});                               // straddles a word boundary
    add({511, 512});                             // straddles a rank block
    { std::vector<uint32_t> v; for (uint32_t p = 0; p < universe; ++p) v.push_back(p);
      add(v); }                                  // full: one maximal run, all-ones fills
    { std::vector<uint32_t> v; for (uint32_t p = 0; p < universe; p += 64) v.push_back(p);
      add(v); }                                  // one bit per word: no fills at all
    { std::vector<uint32_t> v; for (uint32_t p = 0; p < universe; p += 2) v.push_back(p);
      add(v); }                                  // alternating: all literals
    { std::vector<uint32_t> v;                   // leading zero fill then a block
      for (uint32_t p = universe / 2; p < universe / 2 + 300 && p < universe; ++p) v.push_back(p);
      add(v); }
    { std::vector<uint32_t> v;                   // starts with a literal, ends mid-run
      for (uint32_t p = 1; p < 200 && p < universe; ++p) v.push_back(p);
      add(v); }

    char ctx[64];
    std::snprintf(ctx, sizeof ctx, "edge/universe=%u", universe);
    run_all(rows, ctx);
}

// The batched drivers must agree with the per-pair oracle, and the two API forms
// must agree with each other. allpairs_tiles shipped with ZERO callers -- it
// compiled and was listed as delivered without ever being run, which an audit
// caught. This is that gap closed: every policy, both forms, checked against the
// same independent oracle the cell kernels are checked against.
static void check_allpairs() {
    for (uint32_t u : {1024u, 4033u, 16384u})
        for (Structure st : {Structure::Uniform, Structure::Clustered, Structure::Runs})
            for (Spectrum sp : {Spectrum::Uniform, Spectrum::Inverse}) {
                CorpusSpec spec;
                spec.n_rows = 40; spec.universe = u; spec.density = 0.02;
                spec.structure = st; spec.spectrum = sp; spec.seed = 0x9911u + u;
                Corpus c; generate(c, spec);

                uint64_t want = 0;
                for (size_t i = 0; i < c.rows.size(); ++i)
                    for (size_t j = i + 1; j < c.rows.size(); ++j)
                        want += oracle_intersect(c.rows[i], c.rows[j]);

                CostModel m; default_model(m);
                char ctx[96];
                std::snprintf(ctx, sizeof ctx, "allpairs/%s/%s/u=%u",
                              name_of(st), name_of(sp), u);

                for (Policy pol : {Policy::AllBitmap, Policy::PerPair,
                                   Policy::PerTile, Policy::Probe}) {
                    for (uint32_t tile : {8u, 16u, 64u}) {
                        AllPairsStats s1 = allpairs_sum(c.rows, m, pol, tile);
                        ++g_checks;
                        if (s1.sum != want) fail("allpairs", name_of(pol), ctx, s1.sum, want);
                        ++g_checks;
                        if (s1.pairs != (uint64_t)c.rows.size() * (c.rows.size() - 1) / 2)
                            fail("allpairs", "paircount", ctx, s1.pairs, 0);

                        // Form 2 must agree with form 1, and the callback's tile
                        // counts must sum to the same total.
                        uint64_t cb_sum = 0;
                        auto cb = [](uint32_t, uint32_t, uint32_t nr, uint32_t nc,
                                     const uint32_t* cnt, void* vp) {
                            uint64_t* acc = (uint64_t*)vp;
                            // The callback buffer is tile x tile; only nr x nc is live.
                            for (uint32_t a = 0; a < nr; ++a)
                                for (uint32_t b = 0; b < nc; ++b)
                                    *acc += cnt[(size_t)a * 64 + b];
                        };
                        if (tile == 64) {   // cb indexes with the fixed stride 64
                            AllPairsStats s2 = allpairs_tiles(c.rows, m, pol, cb, &cb_sum, tile);
                            ++g_checks;
                            if (s2.sum != want) fail("allpairs_tiles", name_of(pol), ctx, s2.sum, want);
                            ++g_checks;
                            if (cb_sum != want) fail("allpairs_tiles", "callback", ctx, cb_sum, want);
                        }
                    }
                }
            }
}

int main() {
    std::printf("storm kernel-layer differential test\n");
    check_allpairs();

    // Universes chosen to break alignment assumptions: 64 | 4096, and 4033 is
    // not a multiple of 64 (so the last word is partial), 5000 is a multiple of
    // 8 but not 64, 1024 is a whole rank block, 1025 is one bit past one.
    for (uint32_t u : {64u, 128u, 1024u, 1025u, 4033u, 5000u, 8192u, 65536u, 70001u}) {
        edge_cases(u);
    }

    // Random corpora across both generator axes. Small rows-per-corpus keeps
    // the O(n^2 * variants) check affordable while the sweep does the covering.
    const Structure structs[] = {Structure::Uniform, Structure::Clustered, Structure::Runs};
    const Spectrum  specs[]   = {Spectrum::Uniform, Spectrum::Inverse, Spectrum::Bimodal};
    const double    dens[]    = {0.0005, 0.02, 0.3};
    const uint32_t  univs[]   = {1024, 4033, 16384, 65536};

    for (Structure st : structs)
        for (Spectrum sp : specs)
            for (double d : dens)
                for (uint32_t u : univs) {
                    CorpusSpec spec;
                    spec.n_rows    = 6;
                    spec.universe  = u;
                    spec.density   = d;
                    spec.structure = st;
                    spec.spectrum  = sp;
                    spec.seed      = 0x1234u + u + (uint32_t)(d * 100000);
                    Corpus c;
                    generate(c, spec);

                    char ctx[128];
                    std::snprintf(ctx, sizeof ctx, "%s/%s/d=%g/u=%u",
                                  name_of(st), name_of(sp), d, u);
                    run_all(c.rows, ctx);
                }

    std::printf("\n%" PRIu64 " checks, %" PRIu64 " failures\n", g_checks, g_fails);
    if (g_fails) {
        std::printf("\n  failures by variant:\n");
        for (const auto& kv : g_fail_by_variant)
            std::printf("    %-28s %8" PRIu64 "   first: %s\n",
                        kv.first.c_str(), kv.second, g_first_ctx[kv.first].c_str());
        std::printf("FAILED\n");
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
