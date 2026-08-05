/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist. Apache-2.0.
 *
 * Storage cost per representation, reported the way the Roaring papers do:
 * BITS PER VALUE, so corpora of wildly different cardinality are comparable.
 *
 * DATA and METADATA are reported separately and deliberately. The zone map,
 * rank index and per-row header are not part of any representation -- they are
 * what the SELECTOR needs to choose one, and a reader is entitled to know what
 * the selection machinery costs in space as well as time. Roaring has no
 * equivalent line because it has no equivalent structure; folding ours into the
 * data column would hide exactly the thing that is new here.
 *
 * Throughput remains the objective (PROBLEM_STATEMENT: "we ONLY care about
 * throughput, NOT storage"), so this exists to bound the space cost of the
 * speedups, not to claim a compression result.
 */
#include "kernels/storm_repr.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace storm;

int main(int argc, char** argv) {
    const char* path = nullptr;
    uint32_t want = 4096, stride = 1;
    std::string tag = "host";
    bool header = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto nx = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if      (a == "--file")   path = nx();
        else if (a == "--rows")   want = atoi(nx());
        else if (a == "--stride") stride = atoi(nx());
        else if (a == "--tag")    tag = nx();
        else if (a == "--header") header = true;
    }
    if (header) {
        std::printf("corpus,rows,elements,universe,bitmap_bpv,array_bpv,runs_bpv,"
                    "ewah_bpv,best_bpv,meta_bpv,meta_pct_of_best\n");
        if (!path) return 0;
    }
    if (!path) { std::printf("need --file\n"); return 1; }
    if (!stride) stride = 1;

    FILE* f = fopen(path, "rb");
    if (!f) { std::printf("cannot open %s\n", path); return 1; }
    char mg[8]; uint32_t ver, nr, nb, pad;
    if (fread(mg,1,8,f)!=8 || memcmp(mg,"STORMBIN",8) || fread(&ver,4,1,f)!=1 ||
        fread(&nr,4,1,f)!=1 || fread(&nb,4,1,f)!=1 || fread(&pad,4,1,f)!=1) {
        std::printf("bad header\n"); return 1; }

    double B=0, S=0, R=0, W=0, K=0, O=0, best=0;
    uint64_t elems = 0; uint32_t rows = 0, seen = 0;
    std::vector<uint32_t> pos;
    Row r;
    while (rows < want) {
        uint32_t n; if (fread(&n,4,1,f)!=1) break;
        pos.resize(n); if (n && fread(pos.data(),4,n,f)!=n) break;
        if (seen % stride == 0 && n > 0) {
            build_row(r, pos.data(), pos.size(), nb);
            const double b = (double)r.meta.n_words * 8.0;      // dense bitmap
            const double s = (double)n * 4.0;                   // sorted array
            const double rn = (double)r.run_start.size() * 8.0; // runs (start,end)
            const double w = (double)r.ewah.size() * 8.0;       // EWAH
            B += b; S += s; R += rn; W += w;
            K += (double)r.rank.size() * 8.0;                   // rank index
            O += (double)r.occ.size() * 8.0;                    // zone map
            // What a per-row-optimal store would cost: the smallest of the four.
            best += std::min(std::min(b, s), std::min(rn, w));
            elems += n; ++rows;
        }
        ++seen;
    }
    fclose(f);
    if (!rows || !elems) { std::printf("no rows\n"); return 1; }

    // Per-row header: cardinality, run count, word count, first/last set.
    const double hdr = (double)rows * sizeof(RowMeta);
    const double meta = K + O + hdr;
    auto bpv = [&](double bytes){ return bytes * 8.0 / (double)elems; };

    std::printf("%s,%u,%llu,%u,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.1f\n",
                tag.c_str(), rows, (unsigned long long)elems, nb,
                bpv(B), bpv(S), bpv(R), bpv(W), bpv(best), bpv(meta),
                100.0 * meta / best);
    return 0;
}
