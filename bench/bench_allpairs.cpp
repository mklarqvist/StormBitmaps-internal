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

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace storm;

int main(int argc, char** argv) {
    CorpusSpec spec;
    spec.n_rows = 512; spec.universe = 65536; spec.density = 0.01;
    std::string structure = "clustered", spectrum = "inverse", tag = "host";
    uint32_t tile = 64;
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
    }
    spec.structure = structure == "uniform" ? Structure::Uniform
                   : structure == "runs"    ? Structure::Runs : Structure::Clustered;
    spec.spectrum  = spectrum  == "uniform" ? Spectrum::Uniform
                   : spectrum  == "bimodal" ? Spectrum::Bimodal : Spectrum::Inverse;

    Corpus c; generate(c, spec);
    CostModel m; calibrate(m);

    std::printf("# host=%s %s/%s d=%g rows=%u universe=%u tile=%u  (%zu pairs)\n",
                tag.c_str(), structure.c_str(), spectrum.c_str(), spec.density,
                spec.n_rows, spec.universe, tile,
                (size_t)spec.n_rows * (spec.n_rows - 1) / 2);
    std::printf("%-12s %12s %11s %10s %9s %11s\n",
                "policy", "ns/pair", "sel ns/pair", "sel %", "decisions", "checksum");
    std::printf("%s\n", std::string(72, '-').c_str());

    AllPairsStats ref;
    double base = 0;
    for (Policy p : {Policy::AllBitmap, Policy::PerPair, Policy::PerTile, Policy::Probe}) {
        AllPairsStats s = allpairs_sum(c.rows, m, p, tile);
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
        AllPairsStats s = allpairs_sum(c.rows, m, p, tile);
        const double pct = 100.0 * s.ns_selection / s.ns_total;
        std::printf("  %-10s %6.2f%%  %s\n", name_of(p), pct, pct <= 2.0 ? "PASS" : "FAIL");
    }
    return 0;
}
