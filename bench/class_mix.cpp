/*
 * Copyright (c) 2026 Marcus D. R. Klarqvist. Apache-2.0.
 *
 * Which representation class does each ROW fall into?
 *
 * Every speedup table in this project reports an aggregate, which tells a reader
 * that selection helps but not WHERE the benefit comes from. If 99% of rows are
 * single points, the win is a special case for singletons and should be
 * described as one. If rows are spread evenly across classes, the win is the
 * pairing matrix itself. Those are different claims and the aggregates cannot
 * distinguish them.
 *
 * Classes, smallest-representation-wins, in the order a decoder would test:
 *
 *   point   |X| <= 4 -- store the bare 32-bit indices, no run array, no EWAH
 *           header, no rank, no zone map. A one-element row currently allocates
 *           all of those (C33), which is the storage form of the same defect.
 *   array   sorted 32-bit list
 *   runs    RLE as (start,end) pairs
 *   ewah    EWAH-64 compressed bitmap
 *   bitmap  dense
 *   roaring CRoaring's own choice, after run_optimize(), for comparison
 *
 * Reported as a percentage of rows AND as a percentage of elements, because a
 * corpus can be 99% singletons by row while those rows hold 1% of the data.
 */
#include "kernels/storm_repr.h"
#include "roaring.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace storm;

enum Cls { POINT, ARRAY, RUNS, EWAH, BITMAP, NCLS };
static const char* CN[NCLS] = {"point", "array", "runs", "ewah", "bitmap"};

int main(int argc, char** argv) {
    const char* path = nullptr;
    uint32_t want = 4096, stride = 1, point_max = 4;
    std::string tag = "host";
    bool header = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto nx = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if      (a == "--file")      path = nx();
        else if (a == "--rows")      want = atoi(nx());
        else if (a == "--stride")    stride = atoi(nx());
        else if (a == "--point-max") point_max = atoi(nx());
        else if (a == "--tag")       tag = nx();
        else if (a == "--header")    header = true;
    }
    if (header) {
        std::printf("corpus,rows,elements,universe,"
                    "row_point,row_array,row_runs,row_ewah,row_bitmap,"
                    "el_point,el_array,el_runs,el_ewah,el_bitmap,"
                    "roar_array,roar_run,roar_bitset\n");
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

    uint64_t rc[NCLS] = {0}, ec[NCLS] = {0};
    uint64_t ra = 0, rr = 0, rb = 0;           // roaring container counts
    uint64_t elems = 0; uint32_t rows = 0, seen = 0;
    std::vector<uint32_t> pos; Row r;
    while (rows < want) {
        uint32_t n; if (fread(&n,4,1,f)!=1) break;
        pos.resize(n); if (n && fread(pos.data(),4,n,f)!=n) break;
        if (seen % stride == 0 && n > 0) {
            build_row(r, pos.data(), pos.size(), nb);
            const double bmp = (double)r.meta.n_words * 8.0;
            const double arr = (double)n * 4.0;
            const double run = (double)r.run_start.size() * 8.0;
            const double ewh = (double)r.ewah.size() * 8.0;

            Cls c;
            if (n <= point_max)                             c = POINT;
            else {
                double best = arr; c = ARRAY;
                if (run < best) { best = run; c = RUNS; }
                if (ewh < best) { best = ewh; c = EWAH; }
                if (bmp < best) { best = bmp; c = BITMAP; }
            }
            ++rc[c]; ec[c] += n;

            roaring_bitmap_t* rbm = roaring_bitmap_create();
            roaring_bitmap_add_many(rbm, n, pos.data());
            roaring_bitmap_run_optimize(rbm);
            roaring_statistics_t st;
            roaring_bitmap_statistics(rbm, &st);
            ra += st.n_array_containers; rr += st.n_run_containers;
            rb += st.n_bitset_containers;
            roaring_bitmap_free(rbm);

            elems += n; ++rows;
        }
        ++seen;
    }
    fclose(f);
    if (!rows) { std::printf("no rows\n"); return 1; }
    const double R = (double)rows, E = (double)elems;
    const double RC = (double)(ra + rr + rb);

    std::printf("%s,%u,%llu,%u", tag.c_str(), rows, (unsigned long long)elems, nb);
    for (int i = 0; i < NCLS; ++i) std::printf(",%.2f", 100.0 * (double)rc[i] / R);
    for (int i = 0; i < NCLS; ++i) std::printf(",%.2f", 100.0 * (double)ec[i] / E);
    if (RC > 0)
        std::printf(",%.2f,%.2f,%.2f\n", 100.0*ra/RC, 100.0*rr/RC, 100.0*rb/RC);
    else
        std::printf(",0,0,0\n");
    return 0;
}
