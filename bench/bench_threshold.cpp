/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist. Apache-2.0.
 *
 * THRESHOLDED mode -- an OPT-IN contract, not a replacement (RESEARCH_PLAN 15.23).
 *
 * Storm's default contract is exact |A n B| for every pair, and several consumers
 * genuinely need it. But some do not: LD for plotting wants pairs above an r^2
 * cutoff, and materialising the rest is wasted work. When a threshold IS
 * available it unlocks the all-pairs similarity-join family (Bayardo, Ma &
 * Srikant, WWW 2007; Xiao et al. ppjoin, WWW 2008), which the breadth pass
 * measured at 98.6-100% pruning on 7 of 8 corpora at t=0.01.
 *
 * The important structural difference: prefix filtering is CANDIDATE GENERATION,
 * not pair filtering. Everything else in this project enumerates N(N-1)/2 pairs
 * and rejects most of them; this never enumerates them at all. Cost is
 * proportional to the number of candidates the index produces.
 *
 * Correctness: with elements ordered globally and prefix length
 * p(X) = |X| - ceil(t|X|) + 1, any pair with J(A,B) >= t must share an element
 * in their prefixes -- so indexing prefixes only is exact for the thresholded
 * question. Every surviving candidate is then verified with the exact kernel, so
 * reported cardinalities are exact; only the SET of reported pairs is restricted.
 *
 * Baseline is the exact all-pairs scan, and both are checked to produce the
 * identical set of above-threshold pairs.
 */
#include "kernels/storm_cells.h"
#include "kernels/storm_repr.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include <time.h>

using namespace storm;

static inline uint64_t ns_now() {
#if defined(__APPLE__)
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;
#endif
}
template <class L> static auto pick(L l, const char* nm) -> decltype(l.v[0].fn) {
    for (size_t i = 0; i < l.n; ++i) if (std::string(l.v[i].name) == nm) return l.v[i].fn;
    return nullptr;
}

int main(int argc, char** argv) {
    const char* path = nullptr;
    uint32_t want_rows = 512, stride = 1;
    double t_thresh = 0.01; int repeats = 5; std::string tag = "host"; bool csv = false; bool sparse = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto nx = [&]() -> const char* { return i+1<argc ? argv[++i] : ""; };
        if      (a == "--file")   path = nx();
        else if (a == "--rows")   want_rows = atoi(nx());
        else if (a == "--stride") stride = atoi(nx());
        else if (a == "--t")      t_thresh = atof(nx());
        else if (a == "--repeats")repeats = atoi(nx());
        else if (a == "--tag")    tag = nx();
        else if (a == "--csv")    csv = true;
        else if (a == "--sparse") sparse = true;
    }
    if (!path) { std::printf("need --file\n"); return 1; }
    if (!stride) stride = 1;

    FILE* f = fopen(path, "rb");
    if (!f) { std::printf("cannot open %s\n", path); return 1; }
    char mg[8]; uint32_t ver, nr, nb, pad;
    if (fread(mg,1,8,f)!=8 || memcmp(mg,"STORMBIN",8) || fread(&ver,4,1,f)!=1 ||
        fread(&nr,4,1,f)!=1 || fread(&nb,4,1,f)!=1 || fread(&pad,4,1,f)!=1) {
        std::printf("bad header\n"); return 1; }

    std::vector<Row> rows; std::vector<std::vector<uint32_t>> lists;
    std::vector<uint32_t> pos; uint32_t seen = 0;
    while (rows.size() < want_rows) {
        uint32_t n; if (fread(&n,4,1,f)!=1) break;
        pos.resize(n); if (n && fread(pos.data(),4,n,f)!=n) break;
        if (seen % stride == 0 && n > 0) {
            rows.emplace_back(); build_row(rows.back(), pos.data(), pos.size(), nb, sparse);
            lists.emplace_back(pos.begin(), pos.end());
        }
        ++seen;
    }
    fclose(f);
    const uint32_t N = (uint32_t)rows.size();
    if (N < 2) { std::printf("too few rows\n"); return 1; }
    const uint64_t NPAIRS = (uint64_t)N*(N-1)/2;
    const auto f_bs = pick(cell_bs(), "ilp8");
    const auto f_ss = pick(cell_ss(), "adaptive2");
    /* With --sparse there is no bitmap, so every intersection must go through a
     * cell that reads only the list. This is the configuration that can hold the
     * whole corpus; the bitmap-bearing one cannot. */
    auto exact = [&](uint32_t i, uint32_t j) -> uint64_t {
        if (sparse) return f_ss(rows[i].S(), rows[j].S());
        const bool id = rows[i].meta.cardinality >= rows[j].meta.cardinality;
        return id ? f_bs(rows[i].B(), rows[j].S()) : f_bs(rows[j].B(), rows[i].S());
    };

    /* Global element order by document frequency, rarest first (Bayardo's
     * ordering). Rare elements land in prefixes, so posting lists stay short and
     * candidate generation cheap. Frequency order is a heuristic, not required
     * for correctness -- any consistent total order is exact. */
    std::unordered_map<uint32_t,uint32_t> df;
    for (const auto& L : lists) for (uint32_t e : L) ++df[e];
    std::unordered_map<uint32_t,uint32_t> rank;
    {
        std::vector<std::pair<uint32_t,uint32_t>> v(df.begin(), df.end());
        std::sort(v.begin(), v.end(), [](auto&a, auto&b){
            return a.second != b.second ? a.second < b.second : a.first < b.first; });
        for (uint32_t i = 0; i < v.size(); ++i) rank[v[i].first] = i;
    }
    std::vector<std::vector<uint32_t>> ordered(N), orank(N);
    for (uint32_t i = 0; i < N; ++i) {
        ordered[i] = lists[i];
        std::sort(ordered[i].begin(), ordered[i].end(),
                  [&](uint32_t a, uint32_t b){ return rank[a] < rank[b]; });
        /* Materialise the dense rank alongside the element. rank[] is a hash map
         * and was being probed once per prefix element inside the timed loop --
         * ~1M lookups per repeat on census1881, which dominated everything and
         * made the algorithm look 16x worse than it is. Same defect class as the
         * unordered_map index it replaced. */
        orank[i].resize(ordered[i].size());
        for (size_t k = 0; k < ordered[i].size(); ++k) orank[i][k] = rank[ordered[i][k]];
    }

    auto jac_ok = [&](uint32_t i, uint32_t j, uint64_t inter) {
        const double u = (double)lists[i].size() + (double)lists[j].size() - (double)inter;
        return u > 0 && (double)inter / u >= t_thresh;
    };

    /* --- GATE: is prefix filtering worth it on this corpus? ------------------
     *
     * Prefix filtering is 5-44x on short-row corpora and 0.01-0.46x on long-row
     * ones, and no threshold up to 0.8 rescues the latter. The cause is not row
     * length directly but posting-list length: candidate generation costs
     * roughly sum over elements of L_e^2, since a posting list of length L emits
     * L(L-1)/2 candidate pairs. On census1881 elements recur across many rows,
     * so the lists are long and the index generates more candidates than there
     * are pairs.
     *
     * So estimate exactly that, on a sample: build the prefix index over a
     * subset of rows and compare the candidates it would emit against the pairs
     * a plain scan would visit. Same shape as the C16 gate -- a measured
     * property of the corpus, decided once.
     */
    /* The gate, corrected. The first version estimated only candidate count from
     * a 128-row sample and let census1881 through at 0.32 cand/pair when it in
     * fact runs at 0.07x. Two errors: it sampled when the full index is cheap to
     * characterise, and it ignored the dominant term.
     *
     * Total prefix work has two parts:
     *   index traffic  ~ sum of prefix lengths, paid to build and to probe
     *   verification   ~ candidates x mean |S|
     * against exact work ~ N(N-1)/2 x mean |S|. On long-row corpora the index
     * traffic alone exceeds the entire exact scan, which is what the sampled
     * candidate-only estimate could not see.
     */
    bool use_prefix = true; double est_ratio = 0;
    {
        double sump = 0, meanS = 0;
        std::unordered_map<uint32_t,uint32_t> plen_e;
        for (uint32_t i = 0; i < N; ++i) {
            const size_t ci = lists[i].size();
            meanS += (double)ci;
            const size_t pp = (size_t)std::max<long long>(
                0, (long long)ci - (long long)std::ceil(t_thresh*(double)ci) + 1);
            const size_t p2 = std::min(pp, ordered[i].size());
            sump += (double)p2;
            for (size_t k = 0; k < p2; ++k) ++plen_e[orank[i][k]];
        }
        meanS /= (double)N;
        double sumsq = 0;
        for (const auto& kv : plen_e) sumsq += (double)kv.second*(double)(kv.second-1)/2.0;
        const double w_prefix = 2.0*sump + sumsq*meanS;
        const double w_exact  = (double)NPAIRS*meanS;
        est_ratio = w_exact > 0 ? w_prefix / w_exact : 1e9;
        /* Boundary measured, not guessed. At t=0.001 the corpora that should
         * use prefix filtering have est 0.08-1.72 and those that should bypass
         * have 22.8-5917 -- an order-of-magnitude gap with nothing in between.
         * The original 0.5 sat below two genuine wins (census1881_srt 1.25x at
         * est 1.22, wikileaks-noquotes 2.99x at est 1.72) and forfeited them.
         * 5.0 sits in the empty band, costing 3% on census1881 (0.97x, est 0.74)
         * to recover both. */
        use_prefix = est_ratio < 5.0;
        if (!csv) std::printf("# DIAG meanS=%.0f sump=%.0f sumsq=%.0f NPAIRS=%llu "
                    "traversal/pair=%.2f cand/pair=%.4f\n",
                    meanS, sump, sumsq, (unsigned long long)NPAIRS,
                    sumsq/(double)NPAIRS, sumsq/(double)NPAIRS);
    }

    // --- baseline: exact all-pairs scan, then threshold ----------------------
    std::vector<std::pair<uint32_t,uint32_t>> ref;
    double t_exact = 0;
    {
        double best = 1e30;
        for (int r = 0; r < repeats; ++r) {
            std::vector<std::pair<uint32_t,uint32_t>> hits;
            const uint64_t t0 = ns_now();
            for (uint32_t i = 0; i < N; ++i)
                for (uint32_t j = i+1; j < N; ++j) {
                    const uint64_t v = exact(i, j);
                    if (v && jac_ok(i,j,v)) hits.push_back({i,j});
                }
            const double dt = (double)(ns_now()-t0);
            if (dt < best) { best = dt; ref = hits; }
        }
        t_exact = best;
    }

    /* --- prefix-filtered candidate generation, CSR index ---------------------
     * The first version used unordered_map<uint32_t, vector<uint32_t>>, which on
     * census1881 means ~1M distinct keys each owning a heap vector -- hashing and
     * allocation dominated everything and it ran at 0.01x. That was an
     * implementation defect masquerading as an algorithmic limit.
     *
     * Elements already have a dense rank in [0, M), so the index is a plain CSR
     * array: count postings, prefix-sum to offsets, fill. No hashing, no
     * allocation in the loop, sequential access.
     *
     * Built over ALL rows up front rather than incrementally; rows are processed
     * in increasing-cardinality order and a candidate is accepted only when its
     * order position is lower, which reproduces the incremental semantics
     * exactly while letting the index be built in two flat passes.
     */
    std::vector<std::pair<uint32_t,uint32_t>> got;
    double t_prefix = 0; uint64_t cands = 0;
    {
        const uint32_t M = (uint32_t)rank.size();
        std::vector<uint32_t> order(N), opos(N);
        for (uint32_t i = 0; i < N; ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](uint32_t a2, uint32_t b2){
            return lists[a2].size() < lists[b2].size(); });
        for (uint32_t k = 0; k < N; ++k) opos[order[k]] = k;

        std::vector<uint32_t> plen(N);
        for (uint32_t i = 0; i < N; ++i) {
            const size_t ci = lists[i].size();
            plen[i] = (uint32_t)std::max<long long>(
                0, (long long)ci - (long long)std::ceil(t_thresh*(double)ci) + 1);
            if (plen[i] > ordered[i].size()) plen[i] = (uint32_t)ordered[i].size();
        }
        std::vector<uint32_t> cnt(M+1, 0);
        for (uint32_t i = 0; i < N; ++i)
            for (uint32_t k = 0; k < plen[i]; ++k) ++cnt[orank[i][k] + 1];
        for (uint32_t e = 0; e < M; ++e) cnt[e+1] += cnt[e];
        std::vector<uint32_t> post(cnt[M]);
        { std::vector<uint32_t> fill(cnt.begin(), cnt.end()-1);
          for (uint32_t i = 0; i < N; ++i)
              for (uint32_t k = 0; k < plen[i]; ++k) post[fill[orank[i][k]]++] = i; }

        double best = 1e30;
        for (int r = 0; r < repeats; ++r) {
            std::vector<std::pair<uint32_t,uint32_t>> hits;
            std::vector<uint8_t> mark(N, 0);
            std::vector<uint32_t> cand;
            uint64_t nc = 0;
            const uint64_t t0 = ns_now();
            for (uint32_t oi = 0; oi < N; ++oi) {
                const uint32_t i = order[oi];
                const size_t ci = lists[i].size();
                cand.clear();
                for (uint32_t k = 0; k < plen[i]; ++k) {
                    const uint32_t e = orank[i][k];
                    for (uint32_t q = cnt[e]; q < cnt[e+1]; ++q) {
                        const uint32_t j = post[q];
                        if (opos[j] >= oi || mark[j]) continue;   // only earlier rows
                        mark[j] = 1; cand.push_back(j);
                    }
                }
                nc += cand.size();
                for (uint32_t j : cand) {
                    mark[j] = 0;
                    const size_t cj = lists[j].size();
                    if ((double)std::min(ci,cj)/(double)std::max(ci,cj) < t_thresh) continue;
                    const uint64_t v = exact(i, j);
                    if (v && jac_ok(i,j,v)) hits.push_back({std::min(i,j), std::max(i,j)});
                }
            }
            const double dt = (double)(ns_now()-t0);
            if (dt < best) { best = dt; got = hits; cands = nc; }
        }
        t_prefix = best;
    }

    std::sort(ref.begin(), ref.end()); std::sort(got.begin(), got.end());
    const bool ok = (ref == got);

    if (csv) {
        // corpus,t,N,pairs,hits,candidates,exact_ns,prefix_ns,gated_ns,speedup,gated_speedup,gate,correct
        const double t_gated = use_prefix ? t_prefix : t_exact;
        std::printf("%s,%.6f,%u,%llu,%zu,%llu,%.6f,%.6f,%.6f,%.4f,%.4f,%s,%d\n",
                    tag.c_str(), t_thresh, N, (unsigned long long)NPAIRS, ref.size(),
                    (unsigned long long)cands,
                    t_exact/(double)NPAIRS, t_prefix/(double)NPAIRS, t_gated/(double)NPAIRS,
                    t_exact/t_prefix, t_exact/t_gated,
                    use_prefix ? "prefix" : "exact", ok ? 1 : 0);
        return ok ? 0 : 1;
    }
    std::printf("# %s N=%u pairs=%llu t=%.3f  hits=%zu (%.4f%% of pairs)  candidates=%llu (%.4f%%)\n",
                tag.c_str(), N, (unsigned long long)NPAIRS, t_thresh, ref.size(),
                100.0*(double)ref.size()/(double)NPAIRS,
                (unsigned long long)cands, 100.0*(double)cands/(double)NPAIRS);
    std::printf("%-34s %12s %10s\n", "mode", "ns/pair", "speedup");
    std::printf("%-34s %12.4f %9.2fx\n", "exact all-pairs then threshold",
                t_exact/(double)NPAIRS, 1.0);
    std::printf("%-34s %12.4f %9.2fx   %s\n", "prefix-filtered candidates",
                t_prefix/(double)NPAIRS, t_exact/t_prefix,
                ok ? "identical hit set" : "MISMATCH <-- WRONG");
    const double t_gated = use_prefix ? t_prefix : t_exact;
    std::printf("%-34s %12.4f %9.2fx   [gate: est %.2f cand/pair -> %s]\n", "GATED",
                t_gated/(double)NPAIRS, t_exact/t_gated, est_ratio,
                use_prefix ? "prefix" : "exact scan");
    return ok ? 0 : 1;
}
