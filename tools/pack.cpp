/* Preprocessing: choose each row's representation ONCE, at ingest, and store it.
 *
 * PROBLEM_STATEMENT.md §3.2 separates what you STORE from what you COMPUTE IN,
 * and until now the benchmarks ignored that -- every row carried all five
 * representations, built at load time, and the pairing kernels reconstructed
 * views per pair. That is not what a system does. A system picks a
 * representation at ingest, writes it, and at query time pairs whatever it
 * finds.
 *
 * This produces that file. Per row it picks the smallest encoding, with one
 * deliberate bias: a bitmap is preferred when it is within 2x of the smallest,
 * because B x anything is the cheapest cell to pair and the size difference is
 * irrelevant at 626 bytes/row.
 *
 * Universe here is 5,008 bits, so positions fit in uint16 -- halving the array
 * and run payloads against the uint32 form the in-memory Row uses. That is a
 * real advantage of committing to a representation at ingest: you can size the
 * offsets to the universe.
 *
 * STORMPACK layout, little-endian:
 *   "STRMPACK" magic, u32 version=1, u32 n_rows, u32 n_bits, u32 flags
 *   u32 row_offset[n_rows+1]        -- byte offsets into the payload blob
 *   u8  row_tag[n_rows]             -- Repr
 *   payload blob (each row 8-byte aligned)
 */
#include "kernels/storm_repr.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <algorithm>
using namespace storm;

enum Tag : uint8_t { T_BITMAP = 0, T_ARRAY16 = 1, T_RLE16 = 2, T_FULL = 3, T_EMPTY = 4 };
static const char* tname(uint8_t t){ static const char* n[]={"bitmap","array16","rle16","full","empty"}; return n[t]; }

int main(int argc, char** argv) {
    const char* in  = argc > 1 ? argv[1] : "data/chr20.bin";
    const char* out = argc > 2 ? argv[2] : "data/chr20.pack";
    uint32_t stride = argc > 3 ? (uint32_t)atoi(argv[3]) : 1;
    // BIAS: how much larger than the smallest encoding a bitmap may be and still
    // be chosen. bias=0 -> always smallest (greedy packing). bias=1000 -> always
    // bitmap. This is the storage-side analogue of M4's compute-side thresholds,
    // and the point of making it a parameter is that the right value is a
    // Pareto question -- storage bytes against query throughput -- not a
    // constant anyone should be guessing.
    // THROUGHPUT-ONLY selection. Storage size is explicitly not an objective:
    // this data feeds an N x M exact-LD computation, so pairing speed is the
    // whole point and 143 MB vs 1 GB is irrelevant.
    //
    // So the rule is a speed crossover, not a size comparison. A row stored as
    // a sorted array costs ~c probes when paired against a bitmap; stored as a
    // bitmap it costs ~nw/4 SIMD ops however sparse it is. Below the crossover
    // the array wins, above it the bitmap does -- and the crossover moves with
    // the universe size and with cache residency, which is why it is a swept
    // parameter and not a constant.
    const uint32_t card_max_array = argc > 4 ? (uint32_t)atoi(argv[4]) : 32;

    FILE* f = fopen(in, "rb");
    if (!f) { printf("cannot open %s\n", in); return 1; }
    char magic[9] = {0}; uint32_t ver, nr, nb, pad;
    if (fread(magic,1,8,f)!=8 || memcmp(magic,"STORMBIN",8) ||
        fread(&ver,4,1,f)!=1 || fread(&nr,4,1,f)!=1 || fread(&nb,4,1,f)!=1 || fread(&pad,4,1,f)!=1) {
        printf("bad header\n"); return 1; }

    const uint32_t nw = (nb + 63) / 64;
    if (nb > 65536) { printf("universe %u exceeds uint16 positions\n", nb); return 1; }

    std::vector<uint8_t> blob; std::vector<uint32_t> off, rlen; std::vector<uint8_t> tag;
    std::vector<uint32_t> pos; std::vector<uint64_t> bm(nw);
    uint64_t cnt[5] = {0,0,0,0,0};
    uint64_t bytes[5] = {0,0,0,0,0};
    uint32_t kept = 0, seen = 0;

    auto put = [&](const void* p, size_t n) {
        const uint8_t* b = (const uint8_t*)p;
        blob.insert(blob.end(), b, b + n);
    };

    while (true) {
        uint32_t c;
        if (fread(&c,4,1,f) != 1) break;
        pos.resize(c);
        if (c && fread(pos.data(),4,c,f) != c) break;
        if (seen++ % stride) continue;

        // Runs of consecutive set bits.
        uint32_t runs = 0;
        for (uint32_t i = 0; i < c; ) {
            uint32_t j = i; while (j + 1 < c && pos[j+1] == pos[j] + 1) ++j;
            ++runs; i = j + 1;
        }
        const size_t sz_bitmap = (size_t)nw * 8;
        const size_t sz_arr    = (size_t)c * 2;
        const size_t sz_rle    = (size_t)runs * 4;          // u16 start + u16 len

        uint8_t t;
        if (c == 0)                        t = T_EMPTY;
        else if (c == nb)                  t = T_FULL;
        else if (c <= card_max_array) {
            // Sparse enough that probing beats scanning. Prefer runs only when
            // they cut the probe count substantially -- a run costs more per
            // element to walk than an array position does.
            t = (runs * 4 <= c) ? T_RLE16 : T_ARRAY16;
        }
        else                               t = T_BITMAP;
        (void)sz_bitmap; (void)sz_arr; (void)sz_rle;

        while (blob.size() & 7) blob.push_back(0);          // 8-byte align each row
        const uint32_t start = (uint32_t)blob.size();

        if (t == T_BITMAP) {
            std::fill(bm.begin(), bm.end(), 0ull);
            for (uint32_t k = 0; k < c; ++k) bm[pos[k] >> 6] |= 1ull << (pos[k] & 63);
            put(bm.data(), sz_bitmap);
        } else if (t == T_ARRAY16) {
            for (uint32_t k = 0; k < c; ++k) { uint16_t v = (uint16_t)pos[k]; put(&v, 2); }
        } else if (t == T_RLE16) {
            for (uint32_t i = 0; i < c; ) {
                uint32_t j = i; while (j + 1 < c && pos[j+1] == pos[j] + 1) ++j;
                uint16_t st = (uint16_t)pos[i], ln = (uint16_t)(pos[j] - pos[i] + 1);
                put(&st, 2); put(&ln, 2); i = j + 1;
            }
        }
        const uint32_t len = (uint32_t)blob.size() - start;
        cnt[t]++; bytes[t] += len;
        tag.push_back(t); off.push_back(start); rlen.push_back(len); ++kept;
    }
    fclose(f);

    FILE* g = fopen(out, "wb");
    fwrite("STRMPACK", 1, 8, g);
    uint32_t hv = 1, flags = 0;
    fwrite(&hv,4,1,g); fwrite(&kept,4,1,g); fwrite(&nb,4,1,g); fwrite(&flags,4,1,g);
    fwrite(off.data(),  4, kept, g);
    fwrite(rlen.data(), 4, kept, g);
    fwrite(tag.data(),  1, kept, g);
    // pad to 8 so the blob is aligned in the mapped file
    size_t hdr = 24 + 8ull*kept + kept;
    while (hdr & 7) { uint8_t z = 0; fwrite(&z,1,1,g); ++hdr; }
    fwrite(blob.data(), 1, blob.size(), g);
    fclose(g);

    printf("%s: %u rows x %u bits\n", out, kept, nb);
    uint64_t tot = 0; for (int i = 0; i < 5; ++i) tot += bytes[i];
    printf("%-9s %10s %8s %12s %9s\n", "repr", "rows", "%", "bytes", "B/row");
    for (int i = 0; i < 5; ++i) if (cnt[i])
        printf("%-9s %10llu %7.2f%% %12llu %9.1f\n", tname(i),
               (unsigned long long)cnt[i], 100.0*cnt[i]/kept,
               (unsigned long long)bytes[i], (double)bytes[i]/cnt[i]);
    printf("%-9s %10u %7s %12llu %9.1f   (all-bitmap would be %llu)\n", "TOTAL", kept, "",
           (unsigned long long)tot, (double)tot/kept,
           (unsigned long long)((uint64_t)kept * nw * 8));
    return 0;
}
