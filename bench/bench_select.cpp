/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist. Apache-2.0.
 *
 * M4 validation: oracle REGRET (RESEARCH_PLAN.md 5.1, Gate 3 / claim P2).
 *
 * "Report the model's regret -- how much slower than an oracle that always
 * picks correctly. Regret, not raw speed, is the metric that makes this a
 * contribution." Raw speed can be bought by tuning one corpus; regret cannot,
 * because the oracle moves with the data.
 *
 * Three policies, same pairs:
 *   ORACLE     runs every cell and keeps the fastest -- unattainable, the floor
 *   MODEL      predicts from metadata alone and commits (this is M2 + M4)
 *   ALL-BITMAP always B x B -- what every existing implementation does
 *
 * Also reports SELECTION OVERHEAD, which is Gate 1 (claim P2): the per-pair
 * decision must be <= 2% of runtime or the adaptive premise fails.
 */
#include "kernels/storm_cost.h"
#include "kernels/storm_cells.h"
#include "kernels/storm_gen.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include <time.h>

using namespace storm;

static inline uint64_t ns() {
#if defined(__APPLE__)
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec;
#endif
}

template <class L> static auto pick(L list, const char* nm) {
    for (size_t i = 0; i < list.n; ++i)
        if (std::string(list.v[i].name) == nm) return list.v[i].fn;
    return list.v[0].fn;
}

static uint64_t run_pairing(Pairing p, const Row& d, const Row& s) {
    static const auto f_bb = pick(cell_bb(), "occ_sel");
    static const auto f_bs = pick(cell_bs(), "ilp8");
    static const auto f_br = pick(cell_br(), "hybrid4");
    static const auto f_bw = pick(cell_bw(), "skip");
    static const auto f_ss = pick(cell_ss(), "adaptive2");
    static const auto f_sr = pick(cell_sr(), "adaptive2");
    static const auto f_sw = pick(cell_sw(), "adaptive");
    static const auto f_rr = pick(cell_rr(), "adaptive2");
    static const auto f_rw = pick(cell_rw(), "skip");
    static const auto f_ww = pick(cell_ww(), "skip2");
    switch (p) {
        case Pairing::BB: return f_bb(d.B(), s.B());
        case Pairing::BS: return f_bs(d.B(), s.S());
        case Pairing::BR: return f_br(d.B(), s.R());
        case Pairing::BW: return f_bw(d.B(), s.W());
        case Pairing::SS: return f_ss(d.S(), s.S());
        case Pairing::SR: return f_sr(s.S(), d.R());
        case Pairing::SW: return f_sw(s.S(), d.W());
        case Pairing::RR: return f_rr(d.R(), s.R());
        case Pairing::RW: return f_rw(s.R(), d.W());
        case Pairing::WW: return f_ww(d.W(), s.W());
        default:          return 0;
    }
}

int main(int argc, char** argv) {
    CorpusSpec spec;
    spec.n_rows = 128; spec.universe = 65536; spec.density = 0.01;
    std::string structure = "clustered", spectrum = "inverse";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto nx = [&]{ return (i + 1 < argc) ? argv[++i] : (char*)""; };
        if      (a == "--rows")      spec.n_rows   = atoi(nx());
        else if (a == "--universe")  spec.universe = atoi(nx());
        else if (a == "--density")   spec.density  = atof(nx());
        else if (a == "--structure") structure     = nx();
        else if (a == "--spectrum")  spectrum      = nx();
    }
    spec.structure = structure == "uniform" ? Structure::Uniform
                   : structure == "runs"    ? Structure::Runs : Structure::Clustered;
    spec.spectrum  = spectrum  == "uniform" ? Spectrum::Uniform
                   : spectrum  == "bimodal" ? Spectrum::Bimodal : Spectrum::Inverse;

    std::printf("calibrating M4 ... "); std::fflush(stdout);
    CostModel m; calibrate(m);
    std::printf("done\n  ns/unit:");
    for (int i = 0; i < (int)Pairing::Empty; ++i)
        std::printf(" %s=%.3f", name_of((Pairing)i), m.ns_per_unit[i]);
    std::printf("\n\n");

    Corpus c; generate(c, spec);
    struct P { uint32_t d, s; };
    std::vector<P> pairs;
    for (size_t i = 0; i < c.rows.size(); ++i)
        for (size_t j = i + 1; j < c.rows.size(); ++j) {
            const bool id = c.rows[i].meta.cardinality >= c.rows[j].meta.cardinality;
            pairs.push_back({(uint32_t)(id ? i : j), (uint32_t)(id ? j : i)});
        }
    std::printf("corpus: %s/%s d=%g rows=%u universe=%u pairs=%zu\n",
                structure.c_str(), spectrum.c_str(), spec.density,
                spec.n_rows, spec.universe, pairs.size());

    // --- per-pair oracle: time every pairing, keep the best -----------------
    std::vector<Pairing> oracle_choice(pairs.size()), model_choice(pairs.size());
    double oracle_ns = 0;
    for (size_t k = 0; k < pairs.size(); ++k) {
        const Row& d = c.rows[pairs[k].d];
        const Row& s = c.rows[pairs[k].s];
        double bestt = 1e300; Pairing bestp = Pairing::BB;
        for (int i = 0; i < (int)Pairing::Empty; ++i) {
            const Pairing p = (Pairing)i;
            const uint64_t t0 = ns();
            volatile uint64_t sink = 0;
            for (int r = 0; r < 3; ++r) sink += run_pairing(p, d, s);
            const double t = (double)(ns() - t0) / 3.0;
            (void)sink;
            if (t < bestt) { bestt = t; bestp = p; }
        }
        oracle_choice[k] = bestp;
        oracle_ns += bestt;
    }

    // --- the model's choice, and the cost of making it ----------------------
    const uint64_t sel0 = ns();
    for (int rep = 0; rep < 20; ++rep)
        for (size_t k = 0; k < pairs.size(); ++k)
            model_choice[k] = select_pairing(m, c.rows[pairs[k].d].meta,
                                                c.rows[pairs[k].s].meta);
    const double sel_ns_per_pair = (double)(ns() - sel0) / (20.0 * pairs.size());

    auto time_policy = [&](const std::vector<Pairing>* choice, Pairing fixed) {
        uint64_t best = UINT64_MAX;
        for (int rep = 0; rep < 5; ++rep) {
            volatile uint64_t sink = 0;
            const uint64_t t0 = ns();
            for (size_t k = 0; k < pairs.size(); ++k)
                sink += run_pairing(choice ? (*choice)[k] : fixed,
                                    c.rows[pairs[k].d], c.rows[pairs[k].s]);
            const uint64_t dt = ns() - t0; (void)sink;
            best = std::min(best, dt);
        }
        return (double)best / pairs.size();
    };

    const double model_ns  = time_policy(&model_choice, Pairing::BB);
    const double allbm_ns  = time_policy(nullptr, Pairing::BB);
    const double oracle_pp = oracle_ns / pairs.size();

    size_t agree = 0;
    for (size_t k = 0; k < pairs.size(); ++k) agree += (model_choice[k] == oracle_choice[k]);

    std::printf("\n  policy                ns/pair    vs all-bitmap\n  %s\n",
                std::string(52, '-').c_str());
    std::printf("  all-bitmap (B x B)  %9.2f          1.00x\n", allbm_ns);
    std::printf("  M4 model            %9.2f      %8.1fx\n", model_ns, allbm_ns / model_ns);
    std::printf("  per-pair oracle     %9.2f      %8.1fx   (unattainable floor)\n",
                oracle_pp, allbm_ns / oracle_pp);
    std::printf("\n  REGRET (model vs oracle):    %.1f%%\n", 100.0 * (model_ns / oracle_pp - 1.0));
    std::printf("  choice agreement:            %.1f%% of pairs\n",
                100.0 * (double)agree / pairs.size());
    std::printf("\n  selection cost:              %.3f ns/pair = %.2f%% of runtime\n",
                sel_ns_per_pair, 100.0 * sel_ns_per_pair / model_ns);
    std::printf("  GATE 1 (P2, selection <= 2%%): %s\n",
                (sel_ns_per_pair / model_ns <= 0.02) ? "PASS" : "FAIL");
    return 0;
}
