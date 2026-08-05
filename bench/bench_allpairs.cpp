/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist. Apache-2.0.
 *
 * GATE 1 re-test with M3 tile hoisting, and the §6 batched API measured
 * end-to-end for the first time.
 *
 * Per-pair selection measured 28.3% of runtime against a 2% budget. This asks
 * the question RESEARCH_PLAN.md §8 Phase 1 says to ask when that happens: does
 * deciding once per TILE bring it under budget, and what does it cost in
 * decision quality?
 */
#include "kernels/storm_allpairs.h"
#include "kernels/storm_gen.h"
#include "roaring.h"
#include <time.h>

// storm_allpairs.cpp's now_ns() is internal; this is the same clock.
static inline uint64_t bench_ns() {
#if defined(__APPLE__)
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
#endif
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace storm;

/* Load a STORMBIN corpus so the selection policies can be measured on REAL data.
 *
 * Until now this benchmark generated its own corpus, which meant the selector was
 * only ever tested on distributions we chose. The interesting case is a corpus
 * that is genuinely BIMODAL -- gnomAD and UShER both have a median variant deep
 * inside the sparse regime and a small common-variant tail that dominates the
 * mean. That is exactly the shape where one global representation choice must be
 * wrong for one side or the other, and it cannot be constructed convincingly by
 * a generator. */
static bool load_stormbin(Corpus& c, const char* path, uint32_t want_rows,
                          uint32_t stride)
{
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    char mg[8]; uint32_t ver, nr, nb, pad;
    if (std::fread(mg,1,8,f)!=8 || std::memcmp(mg,"STORMBIN",8) ||
        std::fread(&ver,4,1,f)!=1 || std::fread(&nr,4,1,f)!=1 ||
        std::fread(&nb,4,1,f)!=1  || std::fread(&pad,4,1,f)!=1) { std::fclose(f); return false; }
    c.spec.universe = nb; c.rows.clear();
    std::vector<uint32_t> pos; uint32_t seen = 0;
    double sum_card = 0;
    while (c.rows.size() < want_rows) {
        uint32_t n; if (std::fread(&n,4,1,f)!=1) break;
        pos.resize(n); if (n && std::fread(pos.data(),4,n,f)!=n) break;
        if (seen % stride == 0 && n > 0) {
            c.rows.emplace_back();
            build_row(c.rows.back(), pos.data(), pos.size(), nb);
            sum_card += n;
        }
        ++seen;
    }
    std::fclose(f);
    if (c.rows.size() < 2) return false;
    c.spec.n_rows = (uint32_t)c.rows.size();
    c.mean_card = sum_card / c.rows.size();
    c.spec.density = c.mean_card / (double)nb;
    return true;
}

int main(int argc, char** argv) {
    CorpusSpec spec;
    spec.n_rows = 512; spec.universe = 65536; spec.density = 0.01;
    std::string structure = "clustered", spectrum = "inverse", tag = "host";
    uint32_t tile = 64;
    const char* infile = nullptr; uint32_t in_stride = 1; bool no_zm = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto nx = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : ""; };
        if      (a == "--rows")      spec.n_rows   = (uint32_t)atoi(nx());
        else if (a == "--universe")  spec.universe = (uint32_t)atoi(nx());
        else if (a == "--density")   spec.density  = atof(nx());
        else if (a == "--structure") structure     = nx();
        else if (a == "--spectrum")  spectrum      = nx();
        else if (a == "--tile")      tile          = (uint32_t)atoi(nx());
        else if (a == "--tag")       tag           = nx();
        else if (a == "--file")      infile        = nx();
        else if (a == "--in-stride") in_stride     = (uint32_t)atoi(nx());
        else if (a == "--no-zonemap") no_zm        = true;
    }
    spec.structure = structure == "uniform" ? Structure::Uniform
                   : structure == "runs"    ? Structure::Runs : Structure::Clustered;
    spec.spectrum  = spectrum  == "uniform" ? Spectrum::Uniform
                   : spectrum  == "bimodal" ? Spectrum::Bimodal : Spectrum::Inverse;

    Corpus c;
    if (infile) {
        c.spec = spec;
        if (!load_stormbin(c, infile, spec.n_rows, in_stride ? in_stride : 1)) {
            std::printf("FATAL: cannot load %s\n", infile); return 1; }
        spec = c.spec; structure = "real"; spectrum = "real";
    } else generate(c, spec);
    CostModel m; calibrate(m);

    /* CRoaring on the IDENTICAL rows, so "fixed Roaring vs our best" is one
     * measurement rather than two runs stitched together. Built with
     * run_optimize() -- and, when linked against croaring_modified, with the
     * array->bitset promotion of C35 on top, which is the competently-tuned
     * baseline standing rule 9 requires now that a better configuration is known.
     *
     * Pairs here are ALL pairs of the loaded rows, matching the policies below
     * exactly; bench_baseline samples a capped subset and is therefore not
     * directly comparable. */
    double roar_ns = 0; uint64_t roar_sum = 0;
    {
        std::vector<roaring_bitmap_t*> rb(c.rows.size());
        for (size_t i = 0; i < c.rows.size(); ++i) {
            rb[i] = roaring_bitmap_create();
            roaring_bitmap_add_many(rb[i], c.rows[i].list.size(), c.rows[i].list.data());
            roaring_bitmap_run_optimize(rb[i]);
            roaring_bitmap_shrink_to_fit(rb[i]);
#ifdef STORM_CROARING_MODIFIED
            roaring_bitmap_storm_promote_arrays(rb[i], STORM_CTOR_BITSET_THRESHOLD);
#endif
        }
        const uint32_t n = (uint32_t)c.rows.size();
        double best = 1e30;
        for (int r = 0; r < 3; ++r) {
            uint64_t acc = 0;
            const uint64_t t0 = bench_ns();
            for (uint32_t i = 0; i < n; ++i)
                for (uint32_t j = i + 1; j < n; ++j)
                    acc += roaring_bitmap_and_cardinality(rb[i], rb[j]);
            const double dt = (double)(bench_ns() - t0);
            if (dt < best) { best = dt; roar_sum = acc; }
        }
        roar_ns = best / (double)((uint64_t)n * (n - 1) / 2);
        for (auto* b : rb) roaring_bitmap_free(b);
    }


    std::printf("# host=%s %s/%s d=%g rows=%u universe=%u tile=%u  (%zu pairs)\n",
                tag.c_str(), structure.c_str(), spectrum.c_str(), spec.density,
                spec.n_rows, spec.universe, tile,
                (size_t)spec.n_rows * (spec.n_rows - 1) / 2);
    std::printf("%-12s %12s %11s %10s %9s %11s\n",
                "policy", "ns/pair", "sel ns/pair", "sel %", "decisions", "checksum");
    std::printf("%s\n", std::string(72, '-').c_str());

    std::printf("%-12s %12.2f %11s %10s %9s %11llu%s\n",
#ifdef STORM_CROARING_MODIFIED
                "roaring-FIX",
#else
                "roaring",
#endif
                roar_ns, "-", "-", "-", (unsigned long long)roar_sum, "");

    AllPairsStats ref;
    double base = 0;
    for (Policy p : {Policy::AllBitmap, Policy::PerPair, Policy::PerTile, Policy::Probe}) {
        AllPairsStats s = allpairs_sum(c.rows, m, p, tile, no_zm);
        const double nsp = s.ns_total / (double)s.pairs;
        const double sel = s.ns_selection / (double)s.pairs;
        if (p == Policy::AllBitmap) { ref = s; base = nsp; }
        std::printf("%-12s %12.2f %11.3f %9.2f%% %9llu %11llu%s\n",
                    name_of(p), nsp, sel, 100.0 * sel / nsp,
                    (unsigned long long)s.decisions, (unsigned long long)s.sum,
                    s.sum == ref.sum ? "" : "  <-- WRONG");
        if (p != Policy::AllBitmap)
            std::printf("%-12s %12s %11s %9s %9s  %.2fx vs all-bitmap\n",
                        "", "", "", "", "", base / nsp);
    }
    std::printf("\nGATE 1 (P2: selection <= 2%% of runtime)\n");
    for (Policy p : {Policy::PerPair, Policy::PerTile, Policy::Probe}) {
        AllPairsStats s = allpairs_sum(c.rows, m, p, tile, no_zm);
        const double pct = 100.0 * s.ns_selection / s.ns_total;
        std::printf("  %-10s %6.2f%%  %s\n", name_of(p), pct, pct <= 2.0 ? "PASS" : "FAIL");
    }
    return 0;
}
