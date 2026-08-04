/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist. Apache-2.0.
 *
 * N1 — head-to-head against real baselines (RESEARCH_PLAN.md 7.3).
 *
 * Until this existed, every measurement in the project compared Storm against
 * Storm. That makes claims P1 ("beats all-bitmap by >=50x") and P5 ("beats
 * CRoaring on skewed data") UNFALSIFIABLE, which is a fatal review problem
 * independent of how fast anything is. This is the fix.
 *
 * Baselines, per RESEARCH_PLAN.md 7.3, tuned in good faith (standing rule 9):
 *
 *   roaring        CRoaring roaring_bitmap_and_cardinality, containers chosen
 *                  by the library itself
 *   roaring_ro     the same after roaring_bitmap_run_optimize() -- what a
 *                  competent user does, and the configuration that gives
 *                  CRoaring its run containers on clustered data. This is the
 *                  fair comparison and is what the summary line reports against.
 *   storm_bb dense Storm's best pure-SIMD dense kernel: the "all-bitmap" of P1
 *   storm_bb zone  the same cell with zone-map planning
 *   storm <cell>   every other pairing, so the best cell is visible rather than
 *                  asserted
 *
 * All of them compute the same value and are checked against each other on
 * every pair before any is timed.
 *
 * CRoaring is built from the v4.7.2 amalgamation so this compiles with a plain
 * compiler invocation on hosts without cmake.
 */
#include "kernels/storm_cells.h"
#include "kernels/storm_gen.h"

#include "roaring.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <time.h>

using namespace storm;

static inline uint64_t ns_now() {
#if defined(__APPLE__)
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
#endif
}

template <class L>
static auto pick(L list, const char* nm) -> decltype(list.v[0].fn) {
    for (size_t i = 0; i < list.n; ++i)
        if (std::string(list.v[i].name) == nm) return list.v[i].fn;
    return list.v[0].fn;
}

struct P { uint32_t d, s; };

int main(int argc, char** argv) {
    CorpusSpec spec;
    spec.n_rows = 128;
    spec.universe = 65536;
    spec.density = 0.01;
    std::string structure = "clustered", spectrum = "inverse", tag = "host";
    size_t pair_cap = 6000;
    int repeats = 5;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto nx = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : ""; };
        if      (a == "--rows")      spec.n_rows   = (uint32_t)atoi(nx());
        else if (a == "--universe")  spec.universe = (uint32_t)atoi(nx());
        else if (a == "--density")   spec.density  = atof(nx());
        else if (a == "--structure") structure     = nx();
        else if (a == "--spectrum")  spectrum      = nx();
        else if (a == "--mean-run")  spec.mean_run = (uint32_t)atoi(nx());
        else if (a == "--pairs")     pair_cap      = (size_t)atoll(nx());
        else if (a == "--repeats")   repeats       = atoi(nx());
        else if (a == "--tag")       tag           = nx();
    }
    spec.structure = structure == "uniform" ? Structure::Uniform
                   : structure == "runs"    ? Structure::Runs
                                            : Structure::Clustered;
    spec.spectrum  = spectrum  == "uniform" ? Spectrum::Uniform
                   : spectrum  == "bimodal" ? Spectrum::Bimodal
                                            : Spectrum::Inverse;

    Corpus c;
    generate(c, spec);

    // --- the CRoaring corpus, built from the identical positions ------------
    std::vector<roaring_bitmap_t*> rb(c.rows.size()), rbro(c.rows.size());
    double rb_bytes = 0, rbro_bytes = 0;
    for (size_t i = 0; i < c.rows.size(); ++i) {
        rb[i] = roaring_bitmap_create();
        roaring_bitmap_add_many(rb[i], c.rows[i].list.size(), c.rows[i].list.data());
        roaring_bitmap_shrink_to_fit(rb[i]);
        rbro[i] = roaring_bitmap_copy(rb[i]);
        roaring_bitmap_run_optimize(rbro[i]);
        roaring_bitmap_shrink_to_fit(rbro[i]);
        rb_bytes   += (double)roaring_bitmap_portable_size_in_bytes(rb[i]);
        rbro_bytes += (double)roaring_bitmap_portable_size_in_bytes(rbro[i]);
    }

    std::vector<P> pairs;
    for (size_t i = 0; i < c.rows.size() && pairs.size() < pair_cap; ++i)
        for (size_t j = i + 1; j < c.rows.size() && pairs.size() < pair_cap; ++j) {
            const bool id = c.rows[i].meta.cardinality >= c.rows[j].meta.cardinality;
            pairs.push_back({(uint32_t)(id ? i : j), (uint32_t)(id ? j : i)});
        }

    const auto f_bbz = pick(cell_bb(), "occ_sel");
    const auto f_bbp = pick(cell_bb(), "dense");   // portable multi-accumulator; the P1 "all-bitmap" reference
    const auto f_bs  = pick(cell_bs(), "ilp8");
    const auto f_br  = pick(cell_br(), "hybrid4");
    const auto f_bw  = pick(cell_bw(), "skip");
    const auto f_ss  = pick(cell_ss(), "adaptive2");
    const auto f_sr  = pick(cell_sr(), "adaptive2");
    const auto f_rr  = pick(cell_rr(), "adaptive2");
    const auto f_ww  = pick(cell_ww(), "skip2");

    // --- correctness before timing, on every pair ---------------------------
    size_t bad = 0;
    for (size_t k = 0; k < pairs.size(); ++k) {
        const Row& d = c.rows[pairs[k].d];
        const Row& s = c.rows[pairs[k].s];
        const uint64_t want = roaring_bitmap_and_cardinality(rb[pairs[k].d], rb[pairs[k].s]);
        const uint64_t got[] = {
            roaring_bitmap_and_cardinality(rbro[pairs[k].d], rbro[pairs[k].s]),
            f_bbz(d.B(), s.B()), f_bbp(d.B(), s.B()),
            f_bs(d.B(), s.S()),  f_br(d.B(), s.R()), f_bw(d.B(), s.W()),
            f_ss(d.S(), s.S()),  f_sr(s.S(), d.R()), f_rr(d.R(), s.R()),
            f_ww(d.W(), s.W())
        };
        for (uint64_t g : got) if (g != want) { ++bad; break; }
    }
    if (bad) {
        std::printf("FATAL: %zu of %zu pairs disagree between implementations\n",
                    bad, pairs.size());
        return 1;
    }

    auto timeit = [&](auto fn) {
        uint64_t best = UINT64_MAX;
        for (int r = 0; r < repeats; ++r) {
            volatile uint64_t sink = 0;
            const uint64_t t0 = ns_now();
            for (size_t k = 0; k < pairs.size(); ++k) sink += fn(pairs[k]);
            const uint64_t dt = ns_now() - t0;
            (void)sink;
            if (dt < best) best = dt;
        }
        return (double)best / (double)pairs.size();
    };

    const double t_ro   = timeit([&](const P& p){ return roaring_bitmap_and_cardinality(rb[p.d], rb[p.s]); });
    const double t_roro = timeit([&](const P& p){ return roaring_bitmap_and_cardinality(rbro[p.d], rbro[p.s]); });
    const double t_bbp  = timeit([&](const P& p){ return f_bbp(c.rows[p.d].B(), c.rows[p.s].B()); });
    const double t_bbz  = timeit([&](const P& p){ return f_bbz(c.rows[p.d].B(), c.rows[p.s].B()); });
    const double t_bs   = timeit([&](const P& p){ return f_bs (c.rows[p.d].B(), c.rows[p.s].S()); });
    const double t_br   = timeit([&](const P& p){ return f_br (c.rows[p.d].B(), c.rows[p.s].R()); });
    const double t_bw   = timeit([&](const P& p){ return f_bw (c.rows[p.d].B(), c.rows[p.s].W()); });
    const double t_ss   = timeit([&](const P& p){ return f_ss (c.rows[p.d].S(), c.rows[p.s].S()); });
    const double t_sr   = timeit([&](const P& p){ return f_sr (c.rows[p.s].S(), c.rows[p.d].R()); });
    const double t_rr   = timeit([&](const P& p){ return f_rr (c.rows[p.d].R(), c.rows[p.s].R()); });
    const double t_ww   = timeit([&](const P& p){ return f_ww (c.rows[p.d].W(), c.rows[p.s].W()); });

    struct Cand { const char* n; double t; };
    const Cand cells[] = {{"B x B", t_bbz}, {"B x S", t_bs}, {"B x R", t_br},
                          {"B x W", t_bw},  {"S x S", t_ss}, {"S x R", t_sr},
                          {"R x R", t_rr},  {"W x W", t_ww}};
    Cand best = cells[0];
    for (const Cand& x : cells) if (x.t < best.t) best = x;

    const double kB = 1024.0;
    std::printf("# host=%s corpus=%s/%s d=%g rows=%u universe=%u pairs=%zu "
                "card_mean=%.1f runs_mean=%.1f\n",
                tag.c_str(), structure.c_str(), spectrum.c_str(), spec.density,
                spec.n_rows, spec.universe, pairs.size(), c.mean_card, c.mean_runs);
    std::printf("# footprint storm_B=%.0fkB roaring=%.0fkB roaring_ro=%.0fkB zonemap=%.1fkB\n",
                c.bytes_B / kB, rb_bytes / kB, rbro_bytes / kB, c.bytes_occ / kB);
    std::printf("%-24s %10s %12s\n", "kernel", "ns/pair", "vs roaring_ro");
    auto row = [&](const char* n, double t) {
        std::printf("%-24s %10.2f %11.2fx\n", n, t, t_roro / t);
    };
    row("CRoaring", t_ro);
    row("CRoaring run_optimize", t_roro);
    row("storm B x B dense", t_bbp);
    row("storm B x B zonemap", t_bbz);
    row("storm B x S", t_bs);
    row("storm B x R", t_br);
    row("storm B x W", t_bw);
    row("storm S x S", t_ss);
    row("storm S x R", t_sr);
    row("storm R x R", t_rr);
    row("storm W x W", t_ww);
    std::printf("\nBEST storm cell: %s at %.2f ns/pair\n", best.n, best.t);
    std::printf("  vs CRoaring(run_optimize): %8.2fx\n", t_roro / best.t);
    std::printf("  vs CRoaring(default):      %8.2fx\n", t_ro   / best.t);
    std::printf("  vs storm all-bitmap:       %8.2fx   [claim P1]\n", t_bbp / best.t);

    for (size_t i = 0; i < rb.size(); ++i) {
        roaring_bitmap_free(rb[i]);
        roaring_bitmap_free(rbro[i]);
    }
    return 0;
}
