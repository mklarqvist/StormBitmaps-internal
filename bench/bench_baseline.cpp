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

/* Load a STORMBIN corpus (tools/sets2bin.py, tools/gt2bin.py) instead of
 * generating one. Without this the only head-to-head against CRoaring was on
 * synthetic data, so "1.7-19.1x over tuned CRoaring" rested entirely on our own
 * generator -- and a generator can encode the very structure the kernels
 * exploit. Real corpora in the target regime (large universe AND sparse AND
 * skewed) are what make the comparison falsifiable. */
static bool load_stormbin(Corpus& c, const char* path, uint32_t want_rows,
                          uint32_t stride)
{
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    char magic[8]; uint32_t ver, nr, nb, pad;
    if (std::fread(magic, 1, 8, f) != 8 || std::memcmp(magic, "STORMBIN", 8) ||
        std::fread(&ver, 4, 1, f) != 1 || std::fread(&nr, 4, 1, f) != 1 ||
        std::fread(&nb, 4, 1, f) != 1  || std::fread(&pad, 4, 1, f) != 1) {
        std::fclose(f); return false;
    }
    c.spec.universe = nb;
    c.rows.clear();
    std::vector<uint32_t> pos;
    double sum_card = 0, sum_runs = 0, bB = 0, bS = 0, bR = 0, bW = 0, bK = 0, bO = 0;
    uint32_t seen = 0;
    while (c.rows.size() < want_rows) {
        uint32_t n;
        if (std::fread(&n, 4, 1, f) != 1) break;
        pos.resize(n);
        if (n && std::fread(pos.data(), 4, n, f) != n) break;
        // Stride so the sample spans the corpus. Row order is never arbitrary:
        // vertex ids follow crawl/BFS order, variants follow genomic position.
        if (seen % stride == 0 && n > 0) {
            c.rows.emplace_back();
            build_row(c.rows.back(), pos.data(), pos.size(), nb);
            const RowMeta& m = c.rows.back().meta;
            sum_card += m.cardinality; sum_runs += m.n_runs;
            bB += (double)m.n_words * 8.0;
            bS += (double)m.cardinality * 4.0;
            bR += (double)m.n_runs * 8.0;
            bW += (double)c.rows.back().ewah.size() * 8.0;
            bK += (double)c.rows.back().rank.size() * 8.0;
            bO += (double)c.rows.back().occ.size() * 8.0;
        }
        ++seen;
    }
    std::fclose(f);
    if (c.rows.size() < 2) return false;
    c.mean_card = sum_card / c.rows.size();
    c.mean_runs = sum_runs / c.rows.size();
    c.spec.n_rows = (uint32_t)c.rows.size();
    c.spec.density = c.mean_card / (double)nb;
    c.bytes_B = bB; c.bytes_S = bS; c.bytes_R = bR; c.bytes_W = bW;
    c.bytes_rank = bK; c.bytes_occ = bO;
    return true;
}

int main(int argc, char** argv) {
    CorpusSpec spec;
    spec.n_rows = 128;
    spec.universe = 65536;
    spec.density = 0.01;
    std::string structure = "clustered", spectrum = "inverse", tag = "host";
    size_t pair_cap = 6000;
    int repeats = 5;
    const char* infile = nullptr;
    uint32_t in_stride = 1;

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
        else if (a == "--file")      infile        = nx();
        else if (a == "--in-stride") in_stride     = (uint32_t)atoi(nx());
    }
    if (in_stride == 0) in_stride = 1;
    spec.structure = structure == "uniform" ? Structure::Uniform
                   : structure == "runs"    ? Structure::Runs
                                            : Structure::Clustered;
    spec.spectrum  = spectrum  == "uniform" ? Spectrum::Uniform
                   : spectrum  == "bimodal" ? Spectrum::Bimodal
                                            : Spectrum::Inverse;

    Corpus c;
    if (infile) {
        c.spec = spec;
        if (!load_stormbin(c, infile, spec.n_rows, in_stride)) {
            std::printf("FATAL: cannot load STORMBIN corpus %s\n", infile);
            return 1;
        }
        spec = c.spec;
        structure = "real"; spectrum = "real";
    } else {
        generate(c, spec);
    }

    // --- the CRoaring corpus, built from the identical positions ------------
    std::vector<roaring_bitmap_t*> rb(c.rows.size()), rbro(c.rows.size());
    double rb_bytes = 0, rbro_bytes = 0;
    for (size_t i = 0; i < c.rows.size(); ++i) {
        rb[i] = roaring_bitmap_create();
        roaring_bitmap_add_many(rb[i], c.rows[i].list.size(), c.rows[i].list.data());
        roaring_bitmap_shrink_to_fit(rb[i]);
        // Copy BEFORE any Storm promotion pass, so run_optimize() on rbro
        // sees the same stock-CRoaring starting representation it always
        // would -- see the long comment at
        // roaring_bitmap_storm_promote_arrays() (croaring_modified only)
        // for why promoting before run_optimize regresses it.
        rbro[i] = roaring_bitmap_copy(rb[i]);
        roaring_bitmap_run_optimize(rbro[i]);
#ifdef STORM_CROARING_MODIFIED
        // C11 prototype (RESEARCH_PLAN.md 15.3): promote remaining ARRAY
        // containers to BITSET past the measured AND-cardinality compute
        // crossover, not CRoaring's memory break-even DEFAULT_MAX_SIZE.
        roaring_bitmap_storm_promote_arrays(rb[i], -1);
        roaring_bitmap_storm_promote_arrays(rbro[i], -1);
#endif
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

    /* --- zone-map ablation -------------------------------------------------
     * The zone map (1 bit per 512-bit bin, built unconditionally by build_row)
     * is the project's central "move up to planning" mechanism, but until now
     * the table reported it only against `dense` -- the PORTABLE multi-
     * accumulator. On ARM that understates the no-zone-map baseline, because
     * the best index-free kernel is neon_u8, not dense. Comparing a planned
     * kernel against a deliberately weaker unplanned one would flatter the
     * mechanism for the wrong reason.
     *
     * So time, for each cell that has one, the best variant that uses NO side
     * structure alongside the one that does. `needs_rank` in the registry marks
     * every variant reading rank or occ, which is exactly the set to ablate.
     * pick_opt returns null when a variant is absent (e.g. NEON forms on x86)
     * rather than silently falling back to the scalar reference and reporting
     * it as the ablated baseline. */
    auto pick_opt = [](auto list, const char* nm) -> decltype(list.v[0].fn) {
        for (size_t i = 0; i < list.n; ++i)
            if (std::string(list.v[i].name) == nm) return list.v[i].fn;
        return nullptr;
    };
    const auto f_bb_free = pick_opt(cell_bb(), "neon_u8");  // best index-free B x B
    const auto f_bs_occ  = pick_opt(cell_bs(), "occ");      // B x S WITH zone map
    const auto f_br_free = pick_opt(cell_br(), "neon");     // B x R without rank/occ
    const auto f_br_occ  = pick_opt(cell_br(), "occ");      // B x R with zone map
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

    /* How many pairs intersect at all?
     *
     * If most pairs are empty, the workload is not "intersect quickly" but
     * "prove disjointness quickly", and every kernel here is doing the wrong
     * job: B x S spends |S| scattered probes into a bitmap that may be hundreds
     * of kB to establish a zero. That would make an O(1) pre-filter -- a blocked
     * Bloom filter above the zone map -- the highest-value structure in the
     * stack rather than a refinement. The prior is strong on sparse corpora:
     * E[|A n B|] ~ |A||B|/m is 1.3e-4 for as-skitter. Measured here rather than
     * assumed, on the identical pair sample the timings use. */
    size_t empty_pairs = 0;
    uint64_t sum_card = 0;
    for (size_t k = 0; k < pairs.size(); ++k) {
        const uint64_t v = roaring_bitmap_and_cardinality(rb[pairs[k].d], rb[pairs[k].s]);
        if (v == 0) ++empty_pairs;
        sum_card += v;
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
    std::printf("# DISJOINT %zu of %zu pairs (%.1f%%) have |A n B| = 0; mean |A n B| = %.2f\n",
                empty_pairs, pairs.size(), 100.0*(double)empty_pairs/(double)pairs.size(),
                (double)sum_card/(double)pairs.size());
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

    // --- zone-map ablation --------------------------------------------------
    const double t_bbf = f_bb_free ? timeit([&](const P& p){ return f_bb_free(c.rows[p.d].B(), c.rows[p.s].B()); }) : 0.0;
    const double t_bso = f_bs_occ  ? timeit([&](const P& p){ return f_bs_occ (c.rows[p.d].B(), c.rows[p.s].S()); }) : 0.0;
    const double t_brf = f_br_free ? timeit([&](const P& p){ return f_br_free(c.rows[p.d].B(), c.rows[p.s].R()); }) : 0.0;
    const double t_bro = f_br_occ  ? timeit([&](const P& p){ return f_br_occ (c.rows[p.d].B(), c.rows[p.s].R()); }) : 0.0;

    std::printf("\nZONE-MAP ABLATION  (index-free vs index-using, same cell)\n");
    std::printf("%-28s %10s %10s %10s\n", "cell", "no-index", "indexed", "speedup");
    auto abl = [&](const char* n, double free_t, double idx_t) {
        if (free_t <= 0.0 || idx_t <= 0.0) { std::printf("%-28s %10s %10s %10s\n", n, "n/a", "n/a", "--"); return; }
        std::printf("%-28s %10.2f %10.2f %9.2fx\n", n, free_t, idx_t, free_t / idx_t);
    };
    abl("B x B  neon_u8 -> occ_sel", t_bbf, t_bbz);
    abl("B x S  ilp8    -> occ",     t_bs,  t_bso);
    abl("B x R  neon    -> occ",     t_brf, t_bro);
    abl("B x R  neon    -> hybrid4", t_brf, t_br);

    // The headline ablation: best cell when zone-map/rank variants are allowed,
    // against the best cell when they are not. This is the number that says
    // whether the side structure earns its keep at all on this corpus.
    double best_free = 1e30;
    if (t_bbf > 0) best_free = std::min(best_free, t_bbf);
    if (t_brf > 0) best_free = std::min(best_free, t_brf);
    best_free = std::min({best_free, t_bs, t_ss, t_sr, t_rr, t_ww, t_bw});
    std::printf("\nBEST index-free cell:      %8.2f ns/pair\n", best_free);
    std::printf("BEST overall (indexed):    %8.2f ns/pair\n", best.t);
    std::printf("  zone map/rank buys:      %8.2fx\n", best_free / best.t);
    std::printf("  zone map footprint:      %8.3f%% of bitmap bytes\n",
                100.0 * c.bytes_occ / (c.bytes_B > 0 ? c.bytes_B : 1.0));

    for (size_t i = 0; i < rb.size(); ++i) {
        roaring_bitmap_free(rb[i]);
        roaring_bitmap_free(rbro[i]);
    }
    return 0;
}
