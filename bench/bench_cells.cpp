/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist
 * Licensed under the Apache License, Version 2.0.
 *
 * Per-cell microbenchmark for the pairing matrix.
 *
 * One cell, one corpus, every variant raced over the SAME pair list, each
 * checked against the oracle before it is allowed a time. A variant that is
 * faster and wrong cannot be reported as faster.
 *
 * Methodology notes, because RESEARCH_PLAN.md 7 says this is where kernel
 * papers most often fail review:
 *
 *  - MIN of repeats, not mean. The minimum is the estimate of the underlying
 *    cost with scheduler noise removed; the mean measures the machine's mood.
 *  - Every variant runs the identical pair list in the identical order, so
 *    cache state at each call is comparable.
 *  - Cache residency is computed and PRINTED, not left to the reader
 *    (standing rule 5). The corpus footprint per representation is reported
 *    against this host's L1/L2/SLC so a throughput number can be read in
 *    context instead of quoted bare, which is exactly how the legacy README's
 *    114 GB/s claim went wrong.
 *  - Cycles are derived from a MEASURED frequency, not an assumed one, and the
 *    derivation is labelled. macOS exposes no per-core cycle counter to
 *    unprivileged code, so cycles/word here is wall time times a calibrated
 *    frequency -- tier 3 for the time, tier 3-derived for the cycles. It is not
 *    a PMU reading and is not presented as one.
 */
#include "kernels/storm_cells.h"
#include "kernels/storm_gen.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(__APPLE__)
#  include <time.h>
#  include <sys/sysctl.h>
#  include <pthread.h>
#  include <sys/qos.h>
#else
#  include <time.h>
#endif

/* Ask for the performance cores.
 *
 * Apple silicon is heterogeneous: the P-cluster and the E-cluster differ in
 * issue width, cache, and clock, and the scheduler is free to migrate a
 * default-QoS thread between them mid-measurement. That is not noise that more
 * repeats will average out -- it is two different machines being sampled, and
 * it showed up directly as neon_u4 and neon_u8 trading places between runs of
 * the identical binary.
 *
 * QOS_CLASS_USER_INTERACTIVE is a request, not a guarantee, so the reported
 * frequency stays measured per run rather than assumed. */
static void prefer_performance_cores() {
#if defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
}

using namespace storm;

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
static inline uint64_t now_ns() {
#if defined(__APPLE__)
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

/* Calibrate the core frequency with a serially dependent ADD chain.
 *
 * This must be written in asm. The obvious C form (`x += i ^ (x >> 63)`) is a
 * THREE-op dependent chain -- shift, xor, add -- so it retires one iteration
 * per ~3 cycles and under-reports the frequency by about that factor. The first
 * version of this function did exactly that and reported 0.886 GHz for an M4,
 * which is wrong by ~5x and would have propagated into every derived
 * cycles/word figure in the project.
 *
 * `add xN, xN, #1` has a one-cycle latency on every AArch64 core, and each add
 * here depends on the previous one, so the chain cannot be reordered, fused,
 * vectorized or hoisted: the loop retires exactly one add per cycle. Ten adds
 * per iteration amortize the loop overhead (the compare and branch issue in
 * parallel with the chain).
 *
 * Still only the frequency DURING CALIBRATION. On a DVFS part that is not a
 * constant, which is why every cycles/word figure derived from it is labelled
 * derived rather than measured. */
static double measure_ghz() {
    const uint64_t iters = 50000000ull;
    uint64_t best_ns = UINT64_MAX;
    for (int rep = 0; rep < 3; ++rep) {
        uint64_t x = 0;
        const uint64_t t0 = now_ns();
#if defined(__aarch64__)
        for (uint64_t i = 0; i < iters; ++i) {
            __asm__ volatile(
                "add %0, %0, #1\n\t add %0, %0, #1\n\t add %0, %0, #1\n\t"
                "add %0, %0, #1\n\t add %0, %0, #1\n\t add %0, %0, #1\n\t"
                "add %0, %0, #1\n\t add %0, %0, #1\n\t add %0, %0, #1\n\t"
                "add %0, %0, #1"
                : "+r"(x) :: );
        }
        const double per_iter = 10.0;
#elif defined(__x86_64__)
        for (uint64_t i = 0; i < iters; ++i) {
            __asm__ volatile(
                "addq $1, %0\n\t addq $1, %0\n\t addq $1, %0\n\t addq $1, %0\n\t"
                "addq $1, %0\n\t addq $1, %0\n\t addq $1, %0\n\t addq $1, %0\n\t"
                "addq $1, %0\n\t addq $1, %0"
                : "+r"(x) :: );
        }
        const double per_iter = 10.0;
#else
        for (uint64_t i = 0; i < iters; ++i) { __asm__ volatile("" : "+r"(x)); x += 1; }
        const double per_iter = 1.0;
#endif
        const uint64_t t1 = now_ns();
        __asm__ volatile("" :: "r"(x));
        const uint64_t dt = t1 - t0;
        if (dt < best_ns) best_ns = (uint64_t)((double)dt / per_iter);
    }
    return (double)iters / (double)best_ns;
}

// ---------------------------------------------------------------------------
// Pair list. Every cell sees the same pairs in the same order.
//
// Orientation matters for the asymmetric cells: the DENSER row supplies the
// bitmap and the sparser one supplies the list/runs, which is what the
// selection layer (M2) would do. Fixing it here rather than inside each kernel
// keeps the kernels honest about not re-deciding it themselves.
// ---------------------------------------------------------------------------
struct Pair { uint32_t dense, sparse; };

static std::vector<Pair> make_pairs(const Corpus& c, size_t cap) {
    std::vector<Pair> p;
    const size_t n = c.rows.size();
    for (size_t i = 0; i < n && p.size() < cap; ++i)
        for (size_t j = i + 1; j < n && p.size() < cap; ++j) {
            const bool i_dense = c.rows[i].meta.cardinality >= c.rows[j].meta.cardinality;
            p.push_back(Pair{(uint32_t)(i_dense ? i : j), (uint32_t)(i_dense ? j : i)});
        }
    return p;
}

// ---------------------------------------------------------------------------
struct Result {
    std::string cell, variant, note, unit;
    bool     correct   = true;
    bool     needs_rank= false;
    bool     inflates  = false;
    double   ns_pair   = 0;
    double   work      = 0;   // mean work units per pair, in this cell's own unit
    double   ns_unit   = 0;
    double   cyc_unit  = 0;
    uint64_t checksum  = 0;
    uint64_t mismatch_at = UINT64_MAX;
    uint64_t got = 0, want = 0;
    Pair     bad_pair = {0, 0};
};

static double g_ghz      = 1.0;
static int    g_repeats  = 7;
static double g_min_ms   = 30.0;

// Race one cell. `apply` maps (variant fn, pair) to a count.
template <typename F, typename Apply, typename Work>
static void run_cell(const char* cell, const char* unit, VariantList<F> vl,
                     const Corpus& corpus, const std::vector<Pair>& pairs,
                     Apply apply, Work work, std::vector<Result>& out)
{
    // Mean work units per pair, in the unit this cell's cost is proportional to.
    // ns/pair alone cannot distinguish a fast kernel from a corpus that gave it
    // little to do, and comparing cells to each other on ns/pair is meaningless
    // without it -- B x R looking 20x better than B x S may only mean the run
    // array was 20x shorter than the list.
    double total_work = 0;
    for (const Pair& p : pairs) total_work += work(p);
    const double mean_work = pairs.empty() ? 0 : total_work / pairs.size();

    // Reference: the first variant, checked against the oracle over a sample.
    // Every other variant is then checked against it PER PAIR, not by comparing
    // totals. Comparing totals is what this harness did first and it is a bad
    // diagnostic: it proves a variant is wrong without saying where, and two
    // errors of opposite sign cancel into a false pass.
    const size_t sample = std::min<size_t>(pairs.size(), 400);
    std::vector<uint64_t> ref_val(pairs.size());
    for (size_t k = 0; k < pairs.size(); ++k) ref_val[k] = apply(vl.v[0].fn, pairs[k]);

    for (size_t vi = 0; vi < vl.n; ++vi) {
        Result r;
        r.cell       = cell;
        r.variant    = vl.v[vi].name;
        r.note       = vl.v[vi].note ? vl.v[vi].note : "";
        r.needs_rank = vl.v[vi].needs_rank;
        r.inflates   = vl.v[vi].inflates;
        r.unit       = unit;
        r.work       = mean_work;

        // Correctness first. A wrong variant is not timed at all.
        uint64_t sum = 0;
        for (size_t k = 0; k < pairs.size(); ++k) {
            const uint64_t got = apply(vl.v[vi].fn, pairs[k]);
            sum += got;
            const uint64_t want = (k < sample)
                ? oracle_intersect(corpus.rows[pairs[k].dense], corpus.rows[pairs[k].sparse])
                : ref_val[k];
            if (got != want && r.correct) {
                r.correct    = false;
                r.mismatch_at= k;
                r.got = got; r.want = want;
                r.bad_pair   = pairs[k];
            }
        }
        r.checksum = sum;

        out.push_back(r);
    }

    /* Timing, INTERLEAVED across variants.
     *
     * The obvious structure -- time variant 0 to completion, then variant 1,
     * and so on -- is wrong on a thermally managed part. Any monotonic drift
     * over the run (DVFS ramp, thermal throttle, another process arriving)
     * lands unevenly on the variants and systematically favours whichever ran
     * first. That is not a small effect here: the identical binary measured
     * B x B `neon_u8` at 0.64x of the scalar reference in one sweep and 1.17x
     * in the next, with the difference tracking how much work had run before
     * the cell started.
     *
     * Round-robin instead: every repeat times every variant once, so drift is
     * spread evenly and the per-variant MINIMUM is taken over samples drawn
     * from the whole window rather than from one contiguous slice of it.
     */
    std::vector<size_t> live;
    for (size_t vi = 0; vi < vl.n; ++vi)
        if (out[out.size() - vl.n + vi].correct) live.push_back(vi);

    // Calibrate the inner repeat count once, on the reference variant, so every
    // variant is timed over the same amount of work.
    uint64_t inner = 1;
    for (;;) {
        const uint64_t t0 = now_ns();
        volatile uint64_t sink = 0;
        for (uint64_t rep = 0; rep < inner; ++rep) {
            uint64_t acc = 0;
            for (const Pair& p : pairs) acc += apply(vl.v[0].fn, p);
            sink += acc;
        }
        const uint64_t dt = now_ns() - t0;
        (void)sink;
        if (dt >= (uint64_t)(g_min_ms * 1e6) || inner > (1u << 20)) break;
        inner *= 2;
    }

    std::vector<double> best(vl.n, 1e300);
    for (int rep = 0; rep < g_repeats; ++rep) {
        for (size_t vi : live) {
            volatile uint64_t sink = 0;
            const uint64_t t0 = now_ns();
            for (uint64_t it = 0; it < inner; ++it) {
                uint64_t acc = 0;
                for (const Pair& p : pairs) acc += apply(vl.v[vi].fn, p);
                sink += acc;
            }
            const uint64_t dt = now_ns() - t0;
            (void)sink;
            best[vi] = std::min(best[vi], (double)dt / (double)(inner * pairs.size()));
        }
    }

    for (size_t vi : live) {
        Result& r = out[out.size() - vl.n + vi];
        r.ns_pair  = best[vi];
        r.ns_unit  = mean_work > 0 ? best[vi] / mean_work : 0;
        r.cyc_unit = r.ns_unit * g_ghz;
    }
}

// ---------------------------------------------------------------------------
static void print_table(const std::vector<Result>& rs, const Corpus& c,
                        const std::vector<Pair>& pairs, double ghz)
{
    std::string cur;
    double base = 0;
    for (const Result& r : rs) {
        if (r.cell != cur) {
            cur = r.cell;
            base = 0;
            std::printf("\n  %s   (work unit: %s, mean %.1f per pair)\n",
                        cur.c_str(), r.unit.c_str(), r.work);
            std::printf("  %-16s %11s %9s %11s %9s %6s  %s\n",
                        "variant", "ns/pair", "vs ref", "ns/unit", "cyc/unit",
                        "flags", "note");
            std::printf("  %s\n", std::string(110, '-').c_str());
        }
        if (!r.correct) {
            std::printf("  %-16s %12s  WRONG at pair #%" PRIu64 " (rows %u x %u): "
                        "got %" PRIu64 ", expected %" PRIu64 "\n",
                        r.variant.c_str(), "-", r.mismatch_at,
                        r.bad_pair.dense, r.bad_pair.sparse, r.got, r.want);
            continue;
        }
        if (base == 0) base = r.ns_pair;
        char flags[8] = "   ";
        flags[0] = r.needs_rank ? 'K' : ' ';   // needs the rank index
        flags[1] = r.inflates   ? 'I' : ' ';   // inflate-to-bitmap baseline
        flags[2] = '\0';
        std::printf("  %-16s %11.2f %8.2fx %11.4f %9.3f %6s  %s\n",
                    r.variant.c_str(), r.ns_pair, base / r.ns_pair,
                    r.ns_unit, r.cyc_unit, flags, r.note.c_str());
    }
    (void)c; (void)pairs; (void)ghz;
}

// ---------------------------------------------------------------------------
static void usage() {
    std::printf(
        "bench_cells [options]\n"
        "  --rows N          rows in the corpus            (default 256)\n"
        "  --universe N      bits per row                  (default 65536)\n"
        "  --density D       mean set fraction             (default 0.01)\n"
        "  --structure S     uniform|clustered|runs        (default uniform)\n"
        "  --spectrum S      uniform|inverse|bimodal       (default uniform)\n"
        "  --clustering C    P(run continues), clustered   (default 0.9)\n"
        "  --mean-run N      mean run length, runs         (default 32)\n"
        "  --pairs N         cap on pairs benchmarked      (default 20000)\n"
        "  --cells LIST      comma list: bb,bs,br,bw,ss,sr,sw,rr,rw,ww,all\n"
        "  --repeats N       timed repeats, min is taken   (default 5)\n"
        "  --seed N\n"
        "  --json PATH       append one JSON record per result\n");
}

int main(int argc, char** argv) {
    CorpusSpec spec;
    spec.n_rows = 256;
    size_t      pair_cap = 20000;
    std::string cells = "all";
    std::string json_path;

    for (int i = 1; i < argc; ++i) {
        auto next = [&](const char* d) -> const char* {
            return (i + 1 < argc) ? argv[++i] : d;
        };
        const std::string a = argv[i];
        if      (a == "--rows")       spec.n_rows   = (uint32_t)atoi(next("256"));
        else if (a == "--universe")   spec.universe = (uint32_t)atoi(next("65536"));
        else if (a == "--density")    spec.density  = atof(next("0.01"));
        else if (a == "--clustering") spec.clustering = atof(next("0.9"));
        else if (a == "--mean-run")   spec.mean_run = (uint32_t)atoi(next("32"));
        else if (a == "--seed")       spec.seed     = (uint64_t)atoll(next("1"));
        else if (a == "--pairs")      pair_cap      = (size_t)atoll(next("20000"));
        else if (a == "--repeats")    g_repeats     = atoi(next("5"));
        else if (a == "--cells")      cells         = next("all");
        else if (a == "--json")       json_path     = next("");
        else if (a == "--structure") {
            const std::string s = next("uniform");
            spec.structure = (s == "clustered") ? Structure::Clustered
                           : (s == "runs")      ? Structure::Runs
                                                : Structure::Uniform;
        }
        else if (a == "--spectrum") {
            const std::string s = next("uniform");
            spec.spectrum = (s == "inverse") ? Spectrum::Inverse
                          : (s == "bimodal") ? Spectrum::Bimodal
                                             : Spectrum::Uniform;
        }
        else { usage(); return a == "--help" ? 0 : 1; }
    }

    auto want = [&](const char* c) {
        return cells == "all" || cells.find(c) != std::string::npos;
    };

    prefer_performance_cores();
    std::printf("calibrating clock ... ");
    std::fflush(stdout);
    const double ghz = measure_ghz();
    g_ghz = ghz;
    std::printf("%.3f GHz (measured, dependent-add chain)\n", ghz);

    Corpus corpus;
    generate(corpus, spec);
    const std::vector<Pair> pairs = make_pairs(corpus, pair_cap);

    // Standing rule 5: state cache residency with every throughput number.
    const double kB = 1024.0;
    std::printf("\ncorpus: rows=%u universe=%u structure=%s spectrum=%s\n",
                spec.n_rows, spec.universe, name_of(spec.structure), name_of(spec.spectrum));
    std::printf("        cardinality mean=%.1f min=%u max=%u   runs mean=%.1f\n",
                corpus.mean_card, corpus.min_card, corpus.max_card, corpus.mean_runs);
    std::printf("        footprint  B=%.0f kB  S=%.0f kB  R=%.0f kB  W=%.0f kB\n",
                corpus.bytes_B / kB, corpus.bytes_S / kB,
                corpus.bytes_R / kB, corpus.bytes_W / kB);
    std::printf("        indexes    rank=%.0f kB (%.1f%% of B)   zone map=%.1f kB (%.3f%% of B)\n",
                corpus.bytes_rank / kB, 100.0 * corpus.bytes_rank / corpus.bytes_B,
                corpus.bytes_occ / kB, 100.0 * corpus.bytes_occ / corpus.bytes_B);
    std::printf("        host L1d=64 kB  L2=4 MB (P-cluster)  SLC=16 MB\n");
    std::printf("        pairs benchmarked: %zu\n", pairs.size());

    const auto& R = corpus.rows;
    std::vector<Result> results;

    if (want("bs")) run_cell("B x S", "list elt", cell_bs(), corpus, pairs,
        [&](fn_bs f, const Pair& p) { return f(R[p.dense].B(), R[p.sparse].S()); },
        [&](const Pair& p) { return (double)(R[p.sparse].meta.cardinality); }, results);

    if (want("br")) run_cell("B x R", "run", cell_br(), corpus, pairs,
        [&](fn_br f, const Pair& p) { return f(R[p.dense].B(), R[p.sparse].R()); },
        [&](const Pair& p) { return (double)(R[p.sparse].meta.n_runs); }, results);

    if (want("sr")) run_cell("S x R", "elt+run", cell_sr(), corpus, pairs,
        [&](fn_sr f, const Pair& p) { return f(R[p.sparse].S(), R[p.dense].R()); },
        [&](const Pair& p) { return (double)(R[p.sparse].meta.cardinality + R[p.dense].meta.n_runs); }, results);

    if (want("bw")) run_cell("B x W", "ewah word", cell_bw(), corpus, pairs,
        [&](fn_bw f, const Pair& p) { return f(R[p.dense].B(), R[p.sparse].W()); },
        [&](const Pair& p) { return (double)((double)R[p.sparse].ewah.size()); }, results);

    if (want("sw")) run_cell("S x W", "elt+word", cell_sw(), corpus, pairs,
        [&](fn_sw f, const Pair& p) { return f(R[p.sparse].S(), R[p.dense].W()); },
        [&](const Pair& p) { return (double)(R[p.sparse].meta.cardinality + (double)R[p.dense].ewah.size()); }, results);

    if (want("ss")) run_cell("S x S", "elt", cell_ss(), corpus, pairs,
        [&](fn_ss f, const Pair& p) { return f(R[p.dense].S(), R[p.sparse].S()); },
        [&](const Pair& p) { return (double)(R[p.dense].meta.cardinality + R[p.sparse].meta.cardinality); }, results);

    if (want("rr")) run_cell("R x R", "run", cell_rr(), corpus, pairs,
        [&](fn_rr f, const Pair& p) { return f(R[p.dense].R(), R[p.sparse].R()); },
        [&](const Pair& p) { return (double)(R[p.dense].meta.n_runs + R[p.sparse].meta.n_runs); }, results);

    if (want("rw")) run_cell("R x W", "run+word", cell_rw(), corpus, pairs,
        [&](fn_rw f, const Pair& p) { return f(R[p.sparse].R(), R[p.dense].W()); },
        [&](const Pair& p) { return (double)(R[p.sparse].meta.n_runs + (double)R[p.dense].ewah.size()); }, results);

    if (want("ww")) run_cell("W x W", "ewah word", cell_ww(), corpus, pairs,
        [&](fn_ww f, const Pair& p) { return f(R[p.dense].W(), R[p.sparse].W()); },
        [&](const Pair& p) { return (double)((double)R[p.dense].ewah.size() + R[p.sparse].ewah.size()); }, results);

    if (want("bb")) run_cell("B x B", "word", cell_bb(), corpus, pairs,
        [&](fn_bb f, const Pair& p) { return f(R[p.dense].B(), R[p.sparse].B()); },
        [&](const Pair& p) { return (double)(R[p.dense].meta.n_words); }, results);

    print_table(results, corpus, pairs, ghz);

    // The number the whole project is about: the best asymmetric cell against
    // the fixed-cost dense cell, on this corpus.
    double best_bb = 1e300, best_asym = 1e300;
    std::string asym_name;
    for (const Result& r : results) {
        if (!r.correct || r.inflates) continue;
        if (r.cell == "B x B") best_bb = std::min(best_bb, r.ns_pair);
        else if (r.ns_pair < best_asym) { best_asym = r.ns_pair; asym_name = r.cell + "/" + r.variant; }
    }
    if (best_bb < 1e299 && best_asym < 1e299) {
        std::printf("\n  best non-B x B cell: %s at %.2f ns/pair\n", asym_name.c_str(), best_asym);
        std::printf("  B x B floor:         %.2f ns/pair\n", best_bb);
        std::printf("  speedup over fixed-cost dense pairing: %.1fx\n", best_bb / best_asym);
    }

    int bad = 0;
    for (const Result& r : results) bad += !r.correct;
    if (bad) std::printf("\n  %d INCORRECT variant(s) -- see above\n", bad);

    if (!json_path.empty()) {
        FILE* f = fopen(json_path.c_str(), "a");
        if (f) {
            for (const Result& r : results) {
                if (!r.correct) continue;
                fprintf(f,
                    "{\"cell\":\"%s\",\"variant\":\"%s\",\"ns_pair\":%.4f,"
                    "\"rows\":%u,\"universe\":%u,\"density\":%g,"
                    "\"structure\":\"%s\",\"spectrum\":\"%s\",\"clustering\":%g,"
                    "\"mean_run\":%u,\"mean_card\":%.2f,\"mean_runs\":%.2f,"
                    "\"work_unit\":\"%s\",\"work_per_pair\":%.3f,\"ns_unit\":%.5f,"
                    "\"cyc_unit\":%.5f,"
                    "\"pairs\":%zu,\"ghz_measured\":%.4f,\"needs_rank\":%s,"
                    "\"inflate_baseline\":%s,\"checksum\":%" PRIu64 "}\n",
                    r.cell.c_str(), r.variant.c_str(), r.ns_pair,
                    spec.n_rows, spec.universe, spec.density,
                    name_of(spec.structure), name_of(spec.spectrum), spec.clustering,
                    spec.mean_run, corpus.mean_card, corpus.mean_runs,
                    r.unit.c_str(), r.work, r.ns_unit, r.cyc_unit,
                    pairs.size(), ghz, r.needs_rank ? "true" : "false",
                    r.inflates ? "true" : "false", r.checksum);
            }
            fclose(f);
            std::printf("\n  appended %zu records to %s\n", results.size(), json_path.c_str());
        }
    }
    return bad ? 1 : 0;
}
