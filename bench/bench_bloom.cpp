/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist. Apache-2.0.
 *
 * Tier-0 filter experiment (RESEARCH_PLAN.md 15.10).
 *
 * Motivation is 15.9: 95-100% of pairs in the target regime have an EMPTY
 * intersection, so the operation being optimised is disjointness proof. B x S
 * currently establishes a zero by probing the dense side's bitmap |S| times --
 * on uscensus2000 that is 15 scattered accesses into 4.6 MB. The hypothesis is
 * that a small, L1-resident filter absorbs those probes and the bitmap is never
 * touched.
 *
 * What is deliberately NOT tested: "AND the two filters and popcount". That
 * test is sound (any x in A n B has all k bits set in both, so popcount < k
 * proves disjointness) but calibrating it to fire for |A|~15 needs roughly
 * |A||B|/FPR bits -- about as many as the zone map already costs. It buys no
 * space, so it is not the interesting question.
 *
 * THE FAIRNESS CONTROL. Comparing a Bloom filter against the existing 512-bit-
 * bin zone map would confound two variables: size and structure. A hashed
 * filter spreads elements uniformly; a positional zone map benefits from
 * clustering. So every Bloom of N bits is raced against a COARSE ZONE MAP OF
 * EXACTLY N BITS -- same footprint, same cache behaviour, differing only in
 * hashed vs positional bucketing. Without that control a win would be
 * unattributable.
 *
 * All variants are exact: the filter only ever skips a probe it can prove is
 * absent, and every survivor is confirmed against the real bitmap.
 */
#include "kernels/storm_cells.h"
#include "kernels/storm_repr.h"

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
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
#endif
}

template <class L>
static auto pick(L list, const char* nm) -> decltype(list.v[0].fn) {
    for (size_t i = 0; i < list.n; ++i)
        if (std::string(list.v[i].name) == nm) return list.v[i].fn;
    return nullptr;
}

// --- hashing ---------------------------------------------------------------
// Multiply-shift (Dietzfelbinger et al.). Two odd constants give two
// independent-enough hashes for k=2; cheap enough that the filter probe stays
// dominated by the load, which is the point of the experiment.
static inline uint64_t h1(uint32_t x) { return (uint64_t)x * 0x9E3779B97F4A7C15ull; }
static inline uint64_t h2(uint32_t x) { return (uint64_t)x * 0xC2B2AE3D27D4EB4Full; }

struct Filter {
    std::vector<uint64_t> w;
    uint32_t nbits = 0, mask = 0;
    bool blocked = false;

    void init(uint32_t bits, bool blk) {
        nbits = bits; mask = bits - 1; blocked = blk;
        w.assign(bits / 64, 0ull);
    }
    // Blocked: both bits land in ONE 512-bit block, so a probe touches exactly
    // one cache line instead of two (Putze/Sanders/Singler, JEA 2009).
    inline void locate(uint32_t x, uint32_t& i1, uint32_t& i2) const {
        if (blocked) {
            const uint32_t nblk = nbits >> 9;
            const uint32_t blk  = nblk ? (uint32_t)((h1(x) >> 32) % nblk) : 0u;
            const uint32_t base = blk << 9;
            i1 = base + (uint32_t)((h1(x) >> 8) & 511u);
            i2 = base + (uint32_t)((h2(x) >> 8) & 511u);
        } else {
            i1 = (uint32_t)(h1(x) >> 32) & mask;
            i2 = (uint32_t)(h2(x) >> 32) & mask;
        }
    }
    inline void add(uint32_t x) {
        uint32_t a, b; locate(x, a, b);
        w[a >> 6] |= 1ull << (a & 63); w[b >> 6] |= 1ull << (b & 63);
    }
    inline bool maybe(uint32_t x) const {
        uint32_t a, b; locate(x, a, b);
        return ((w[a >> 6] >> (a & 63)) & 1u) && ((w[b >> 6] >> (b & 63)) & 1u);
    }
};

// Coarse positional zone map at a chosen bit budget -- the fairness control,
// and after C14 the winning structure.
struct CoarseOcc {
    std::vector<uint64_t> w;
    uint32_t shift = 0, nbits = 0, nwords = 0;

    void init(uint32_t bits, uint32_t universe) {
        nbits = bits;
        uint32_t width = (universe + bits - 1) / bits;   // bits of universe per bucket
        shift = 0; while ((1u << shift) < width) ++shift; // round up to a power of two
        // The shift, not the requested budget, decides how many buckets the
        // universe actually spans. Allocating `bits` when the rounded-up shift
        // needs fewer would leave a tail that add() must bounds-check on every
        // call; sizing from the shift lets the hot path drop the check.
        nwords = (uint32_t)((((uint64_t)universe >> shift) + 1 + 63) / 64);
        w.assign(nwords, 0ull);
    }
    inline void add(uint32_t x) {
        const uint32_t i = x >> shift;
        w[i >> 6] |= 1ull << (i & 63);
    }
    inline bool maybe(uint32_t x) const {
        const uint32_t i = x >> shift;
        return (w[i >> 6] >> (i & 63)) & 1u;
    }
    size_t bytes() const { return w.size() * 8; }
};

/* Size the filter to the ROW's cardinality, not to the universe or a global
 * constant.
 *
 * C14 found a fixed 2 kB budget beats the m/512-proportional map, but a fixed
 * budget is only right on average: it over-provisions a 15-element neighbourhood
 * (as-skitter) and under-provisions a 5,019-element census attribute. Fill rate
 * is what governs selectivity, so hold fill roughly constant at 1/K and let the
 * width follow |A|. For as-skitter that is a 64-byte filter -- comfortably
 * L1-resident, and the probe is then a guaranteed L1 hit rather than a hopeful
 * one. Clamped so a huge row cannot reintroduce the cache problem the fixed
 * budget was solving. */
static uint32_t adaptive_bits(uint32_t card, uint32_t K, uint32_t lo, uint32_t hi) {
    uint64_t want = (uint64_t)card * K;
    uint32_t b = lo;
    while (b < want && b < hi) b <<= 1;
    return b > hi ? hi : b;
}

int main(int argc, char** argv) {
    const char* path = nullptr;
    uint32_t want_rows = 200, stride = 1, fbits = 4096, kfill = 32;
    size_t pair_cap = 20000; int repeats = 7;
    std::string tag = "host";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto nx = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if      (a == "--file")   path = nx();
        else if (a == "--rows")   want_rows = atoi(nx());
        else if (a == "--stride") stride = atoi(nx());
        else if (a == "--bits")   fbits = atoi(nx());
        else if (a == "--kfill")  kfill = atoi(nx());
        else if (a == "--pairs")  pair_cap = atoll(nx());
        else if (a == "--repeats")repeats = atoi(nx());
        else if (a == "--tag")    tag = nx();
    }
    if (!path) { std::printf("need --file\n"); return 1; }
    if (stride == 0) stride = 1;

    FILE* f = fopen(path, "rb");
    if (!f) { std::printf("cannot open %s\n", path); return 1; }
    char magic[8]; uint32_t ver, nr, nb, pad;
    if (fread(magic,1,8,f)!=8 || memcmp(magic,"STORMBIN",8) ||
        fread(&ver,4,1,f)!=1 || fread(&nr,4,1,f)!=1 ||
        fread(&nb,4,1,f)!=1  || fread(&pad,4,1,f)!=1) { std::printf("bad header\n"); return 1; }

    std::vector<Row> rows; std::vector<uint32_t> pos;
    std::vector<Filter> bl, blb; std::vector<CoarseOcc> co, ad;
    uint32_t seen = 0;
    while (rows.size() < want_rows) {
        uint32_t n; if (fread(&n,4,1,f)!=1) break;
        pos.resize(n); if (n && fread(pos.data(),4,n,f)!=n) break;
        if (seen % stride == 0 && n > 0) {
            rows.emplace_back();
            build_row(rows.back(), pos.data(), pos.size(), nb);
            bl.emplace_back();  bl.back().init(fbits, false);
            blb.emplace_back(); blb.back().init(fbits, true);
            co.emplace_back();  co.back().init(fbits, nb);
            ad.emplace_back();  ad.back().init(adaptive_bits(n, kfill, 512u, fbits), nb);
            for (uint32_t k = 0; k < n; ++k) {
                bl.back().add(pos[k]); blb.back().add(pos[k]); co.back().add(pos[k]); ad.back().add(pos[k]);
            }
        }
        ++seen;
    }
    fclose(f);
    if (rows.size() < 2) { std::printf("too few rows\n"); return 1; }

    // Strided walk of the upper triangle -- never a truncated row-major fill.
    struct P { uint32_t d, s; };
    std::vector<P> pairs;
    {
        const uint64_t n = rows.size(), T = n*(n-1)/2;
        const uint64_t target = std::min<uint64_t>(pair_cap, T);
        const uint64_t step = target ? std::max<uint64_t>(1, T/target) : 1;
        for (uint64_t k = 0; k < T && pairs.size() < target; k += step) {
            uint64_t i = 0, acc = 0;
            while (i + 1 < n && acc + (n-1-i) <= k) { acc += (n-1-i); ++i; }
            const uint64_t j = k - acc + i + 1;
            if (j <= i || j >= n) continue;
            const bool id = rows[i].meta.cardinality >= rows[j].meta.cardinality;
            pairs.push_back({(uint32_t)(id?i:j), (uint32_t)(id?j:i)});
        }
    }

    const auto f_ilp8 = pick(cell_bs(), "ilp8");
    const auto f_occ  = pick(cell_bs(), "occ");

    // Filtered probe: skip anything the filter proves absent, confirm the rest
    // against the real bitmap. Exact by construction.
    auto probe = [&](const auto& filt, const BitmapView& B, const ListView& S) {
        uint64_t hit = 0;
        for (uint32_t i = 0; i < S.n; ++i) {
            const uint32_t x = S.v[i];
            if (!filt.maybe(x)) continue;
            hit += (B.w[x >> 6] >> (x & 63)) & 1ull;
        }
        return hit;
    };

    /* Group-collapsing probe. The list is sorted, so consecutive elements often
     * share a bucket; probe once per distinct bucket and skip the whole group on
     * a miss. Replaces repeated filter lookups with a shift-and-compare scan,
     * which is where the large-|S| corpora were losing. */
    auto probe_skip = [&](const CoarseOcc& f, const BitmapView& B, const ListView& S) {
        uint64_t hit = 0; uint32_t i = 0;
        while (i < S.n) {
            const uint32_t b = S.v[i] >> f.shift;
            uint32_t j = i + 1;
            while (j < S.n && (S.v[j] >> f.shift) == b) ++j;
            if ((f.w[b >> 6] >> (b & 63)) & 1u)
                for (uint32_t k = i; k < j; ++k) {
                    const uint32_t x = S.v[k];
                    hit += (B.w[x >> 6] >> (x & 63)) & 1ull;
                }
            i = j;
        }
        return hit;
    };

    // --- correctness before timing -----------------------------------------
    size_t bad = 0, empties = 0;
    for (const P& p : pairs) {
        const uint64_t want = f_ilp8(rows[p.d].B(), rows[p.s].S());
        if (!want) ++empties;
        if (probe(bl [p.d], rows[p.d].B(), rows[p.s].S()) != want ||
            probe(blb[p.d], rows[p.d].B(), rows[p.s].S()) != want ||
            probe(co [p.d], rows[p.d].B(), rows[p.s].S()) != want ||
            probe(ad [p.d], rows[p.d].B(), rows[p.s].S()) != want ||
            probe_skip(co[p.d], rows[p.d].B(), rows[p.s].S()) != want ||
            probe_skip(ad[p.d], rows[p.d].B(), rows[p.s].S()) != want ||
            (f_occ && f_occ(rows[p.d].B(), rows[p.s].S()) != want)) ++bad;
    }
    if (bad) { std::printf("FATAL: %zu/%zu disagree\n", bad, pairs.size()); return 1; }

    // --- filter selectivity: what fraction of probes survive the filter? ----
    uint64_t tot_probes = 0, surv_bl = 0, surv_blb = 0, surv_co = 0;
    for (const P& p : pairs) {
        const ListView S = rows[p.s].S();
        tot_probes += S.n;
        for (uint32_t i = 0; i < S.n; ++i) {
            surv_bl  += bl [p.d].maybe(S.v[i]);
            surv_blb += blb[p.d].maybe(S.v[i]);
            surv_co  += co [p.d].maybe(S.v[i]);
        }
    }

    // --- interleaved timing -------------------------------------------------
    // Round-robin across variants inside each repeat, so thermal drift and
    // frequency ramp hit every variant equally. Timing each variant to
    // completion in turn is what made the corpus sweep drift up to 1.65x.
    enum { V_ILP8, V_OCC, V_BLOOM, V_COARSE, V_ADAPT, V_CSKIP, V_ASKIP, NV };
    const char* names[NV] = {"B x S ilp8 (no filter)", "B x S zone map (512b bins)",
                             "bloom k=2", "coarse fixed", "coarse adaptive",
                             "coarse fixed +skip", "coarse adaptive +skip"};
    double best[NV]; for (int v = 0; v < NV; ++v) best[v] = 1e30;
    for (int r = 0; r < repeats; ++r) {
        for (int v = 0; v < NV; ++v) {
            if (v == V_OCC && !f_occ) continue;
            volatile uint64_t sink = 0;
            const uint64_t t0 = ns_now();
            for (const P& p : pairs) {
                switch (v) {
                    case V_ILP8:    sink += f_ilp8(rows[p.d].B(), rows[p.s].S()); break;
                    case V_OCC:     sink += f_occ (rows[p.d].B(), rows[p.s].S()); break;
                    case V_BLOOM:   sink += probe(bl [p.d], rows[p.d].B(), rows[p.s].S()); break;
                    case V_COARSE:  sink += probe(co [p.d], rows[p.d].B(), rows[p.s].S()); break;
                    case V_ADAPT:   sink += probe(ad [p.d], rows[p.d].B(), rows[p.s].S()); break;
                    case V_CSKIP:   sink += probe_skip(co[p.d], rows[p.d].B(), rows[p.s].S()); break;
                    case V_ASKIP:   sink += probe_skip(ad[p.d], rows[p.d].B(), rows[p.s].S()); break;
                }
            }
            const double dt = (double)(ns_now() - t0) / (double)pairs.size();
            (void)sink;
            if (dt < best[v]) best[v] = dt;
        }
    }

    const double bitmap_kb = (double)rows[0].meta.n_words * 8.0 / 1024.0;
    std::printf("# %s universe=%u rows=%zu pairs=%zu filter=%u bits (%.0f B/row) "
                "bitmap=%.0f kB/row disjoint=%.1f%%\n",
                tag.c_str(), nb, rows.size(), pairs.size(), fbits, fbits/8.0,
                bitmap_kb, 100.0*(double)empties/(double)pairs.size());
    { double abytes=0; for (const auto& a : ad) abytes += (double)a.bytes();
      std::printf("# adaptive filter: mean %.0f B/row (K=%u), fixed %.0f B/row\n",
                  abytes/(double)ad.size(), kfill, fbits/8.0); }
    std::printf("# filter survival: bloom %.3f%%  blocked %.3f%%  coarse-occ %.3f%%  "
                "(of %llu probes)\n",
                100.0*(double)surv_bl/(double)tot_probes,
                100.0*(double)surv_blb/(double)tot_probes,
                100.0*(double)surv_co/(double)tot_probes,
                (unsigned long long)tot_probes);
    std::printf("%-30s %10s %10s\n", "variant", "ns/pair", "vs ilp8");
    for (int v = 0; v < NV; ++v) {
        if (best[v] > 1e29) continue;
        std::printf("%-30s %10.2f %9.2fx\n", names[v], best[v], best[V_ILP8]/best[v]);
    }
    return 0;
}
